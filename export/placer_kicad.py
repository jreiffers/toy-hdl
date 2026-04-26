from kipy import KiCad

from kipy.geometry import Vector2, Angle
from absl import app
from absl import flags
from collections import namedtuple
import math
import itertools
from scipy.spatial import ConvexHull
import numpy as np
from collections.abc import Callable
from multiprocessing import Pool
import functools
import random
from scipy.spatial import distance_matrix
from scipy.sparse.csgraph import minimum_spanning_tree
from collections import defaultdict

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')
flags.DEFINE_integer('target_cluster_size', 0, 'Merge clusters up to a cumulative size of this.')
flags.DEFINE_bool('limit_cluster_size', False, 'Fail if clusters larger than cluster_size are created.')

from util import get_connections, group_transistors_by_hierarchy, group_transistor_nets, normalize, open_net

Slot = namedtuple('Slot', ['u', 'v'])
TransistorId = str
SPACING = 4

uv_base_angle = 0

def to_xy(slot: Slot):
    u_rad = math.radians(uv_base_angle)
    v_rad = math.radians(uv_base_angle + 60)

    ux, uy = math.cos(u_rad), math.sin(u_rad)
    vx, vy = math.cos(v_rad), math.sin(v_rad)

    u, v = slot
    return (u * ux + v * vx) * SPACING, (u * uy + v * vy) * SPACING

class Cost:
    def __init__(self, distance):
        self.distance = distance

    def val(self):
        return self.distance

    def __lt__(self, other):
        return self.val() < other.val()

    def __repr__(self):
        return f'{self.val()}'


def degenerate_score(pts):
    pts = np.array(pts)
    if len(pts) < 2:
        return 0.0
    pts = pts - pts[0]
    rank = np.linalg.matrix_rank(pts, tol=1e-9)
    if rank > 1:
        return None

    diff = np.max(pts, axis=0) - np.min(pts, axis=0)
    return np.sqrt(np.sum(diff**2)) * 2


score_cache = dict()


def distance_score(placement: Callable[[TransistorId],
                                       Slot | None],
                   group: set[TransistorId],
                   to_xy) -> float:
    pts = []
    for b in group:
        pos = placement(b)
        if pos is None:
            continue
        tx, ty = to_xy(pos)
        pts.append([tx, ty])

    if (len(pts) < 2):
        return 0

    pts_set = frozenset([(x, y) for x, y in pts])
    if pts_set in score_cache:
        return score_cache[pts_set]

    pts = np.array(pts)

    dist_matrix = distance_matrix(pts, pts)
    mst = minimum_spanning_tree(dist_matrix)

    # Larger trees have more opportunity to interfere with other nets.
    # But really we care more about the area.
    score = mst.sum() ** 1.5

    o1 = pts + np.array([-0.8, -0.8])
    o2 = pts + np.array([-0.8, 0.8])
    o3 = pts + np.array([0.8, 0])
    pts2 = np.concatenate([o1,o2,o3])

    hull = ConvexHull(points=pts2)
    # volume: area, area: perimeter
    score += hull.volume

    # 1) Penalty per row
    # 2) Penalty for empty slots in rows
    rows = defaultdict(list)
    for b in group:
        pos = placement(b)
        if pos is None:
            continue
        u, v = pos
        rows[v].append(u)


    score += len(rows) * 25
    for row in rows.values():
        empty = (max(row) - min(row) + 1) - len(row)
        if empty == 0:
            score -= len(row) ** 1.5
        
        score += empty * 15

    score_cache[pts_set] = score
    return score


def cost(placement: dict[TransistorId, Slot], groups: list[set[TransistorId]]):
    dist = 0
    # TODO: connected to something outside the group -> should be on the edge?
    for g in groups:
        dist += distance_score(
            lambda t: placement[t] if t in placement else None, g, to_xy)

    return Cost(dist)

class ChangeSlot:
    def __init__(self, transistor, slot):
        self.transistor = transistor
        self.slot = slot

    def is_valid(self, placement):
        return self.slot not in placement.values()

    def apply(self, placement):
        self.slot, placement[self.transistor] = placement[self.transistor], self.slot

    def unapply(self, placement):
        self.apply(placement)

    def __repr__(self):
        return f"ChangeSlot({self.transistor}, {self.slot})"

class SwapTwo:
    def __init__(self, a, b):
        self.a = a
        self.b = b

    def is_valid(self, placement):
        return self.a in placement and self.b in placement

    def apply(self, placement):
        placement[self.a], placement[self.b] = placement[self.b], placement[self.a]

    def unapply(self, placement):
        self.apply(placement)

    def __repr__(self):
        return f"SwapTwo({self.a}, {self.b})"

class RotateThree:
    def __init__(self, a, b, c):
        self.a = a
        self.b = b
        self.c = c

    def is_valid(self, placement):
        return self.a in placement and self.b in placement and self.c in placement

    def apply(self, placement):
        placement[self.a], placement[self.b], placement[self.c] = placement[self.b], placement[self.c], placement[self.a]

    def unapply(self, placement):
        self.apply(placement)
        self.apply(placement)

    def __repr__(self):
        return f"RotateThree({self.a}, {self.b}, {self.c})"

def swap_two(placement: dict[TransistorId, Slot]):
    for a in placement.keys():
        for b in placement.keys():
            if a >= b:
                continue
            yield SwapTwo(a, b)

def rotate_some(placement: dict[TransistorId, Slot], n: int):
    if len(placement) < 3:
        return
    k = list(placement.keys())
    for i in range(n):
        yield RotateThree(*random.sample(k, 3))

def use_free_slots(placement: dict[TransistorId, Slot], free_slots):
    for a in placement.keys():
        for slot in free_slots:
            yield ChangeSlot(a, slot)

def eval_candidate(cost_fn, base_cost, placement, candidate):
    if not candidate.is_valid(placement):
        return None

    candidate.apply(placement)
    new_cost = cost_fn(placement)
    candidate.unapply(placement)

    if not (new_cost < base_cost):
        return None
    return candidate, new_cost


def optimize_placement(transistor_names: list[TransistorId],
                       available_slots: list[Slot],
                       cost_fn) -> dict[TransistorId, Slot]:
    assert len(available_slots) >= len(transistor_names)

    # Generate an initial placement.
    placement = dict()
    for name, slot in zip(transistor_names, available_slots):
        placement[name] = slot

    def free_slots():
        slots = set(available_slots)
        for slot in placement.values():
            slots.remove(slot)
        return slots

    current_cost = cost_fn(placement)
    cont = True

    with Pool(16) as p:
      while cont:
        print(current_cost)
        assert len(set(placement.items())) == len(placement)

        eval_fn = functools.partial(eval_candidate, cost_fn, current_cost, placement)
        candidates = itertools.chain(
                    use_free_slots(placement, free_slots()), swap_two(placement), rotate_some(placement, 20000))
        candidates_and_scores = p.map(eval_fn, candidates)
        #candidates_and_scores = [eval_candidate(cost_fn, current_cost, placement, candidate) for candidate in candidates]

        candidates_and_scores = [x for x in candidates_and_scores if x is not None]
        if not candidates_and_scores:
            break
        
        #candidates_and_scores.sort(key=lambda a: a[1])

        any_success = False
        for (c, pred) in candidates_and_scores:
            if c.is_valid(placement):
                r = eval_candidate(cost_fn, current_cost, placement, c)
                if r is None:
                    print(f'  verification failed {c}')
                else:
                    any_success = True
                    c.apply(placement)
                    print(f'  {current_cost} -> {r[1]} {c} (predicted {pred})')
                    current_cost = r[1]
        
        if not any_success:
            print('verification of all candidates failed')
            break

    return placement


def cluster_diameter(n: int) -> int:
    shell = 1
    n -= 1
    while n > 0:
        n -= 6 * shell
        shell += 1
    return shell * 2 - 1


def generate_hex_slots(n: int) -> list[Slot]:
    AXES = [(-1, 1), (0, 1), (1, 0), (1, -1), (0, -1), (-1, 0)]

    def gen_shells():
        yield Slot(0, 0)
        shell = 1
        to_gen = n
        while to_gen > 0:
            for a in range(6):
                su = AXES[a][0] * shell
                sv = AXES[a][1] * shell
                du, dv = AXES[(a + 2) % 6]

                for b in range(shell):
                    to_gen -= 1
                    yield Slot(su + du * b, sv + dv * b)

            shell += 1

    return list(gen_shells())


def generate_cluster_grid(n: int) -> list[Slot]:
    if n == 1:
        return [Slot(0, 0)]

    def num_slots(h, w) -> int:
        return h * (w-1) + h//2

    def gen_slots(h, w) -> list[Slot]:
        out = []
        for v in range(h):
            for u in range(w if v % 2 == 1 else w-1):
                out.append(Slot(u - (v+1)//2, v))
        return out

    best_score = float('inf')
    best_size = (None, None)
    
    def score(h, w) -> int:
        return num_slots(h, w) - n + abs(h-w) * 3

    # Nice rectangles have an odd height and any width.
    for h in range(1, n, 2):
        w = n // h + 1
        if num_slots(h, w-1) >= n:
            w -= 1
        assert num_slots(h, w) >= n

        s = score(h, w)
        if s < best_score:
            best_score = s
            best_size = (h, w)

    return gen_slots(*best_size)

def get_clusters(net) -> list[list[TransistorId]]:
    flat_hierarchy: list[list[TransistorId]] = []

    def flatten_hierarchy(group, path='/'):
        for (name, subgroup) in group["children"].items():
            flatten_hierarchy(subgroup, f'{path}/{name}')

        this_group = [normalize(id) for id in group["transistors"].keys()]
        if this_group:
            print(f'Group {path}: {len(this_group)}')
            flat_hierarchy.append(this_group)
    
    flatten_hierarchy(group_transistors_by_hierarchy(net))

    merged = []
    out = []
    for g in flat_hierarchy:
        if len(merged) + len(g) > FLAGS.target_cluster_size:
            if merged:
                out.append(merged)
                merged = []
        merged += g
    if merged:
        out.append(merged)

    if FLAGS.limit_cluster_size:
        for g in out:
            assert len(g) <= FLAGS.target_cluster_size

    for g in out:
        print("Cluster size:", len(g))

    return out

def place_group(terminal_groups, group):
    slots = generate_hex_slots(len(group))
    score_cache = dict()
    fn = functools.partial(cost, groups=terminal_groups)
    return optimize_placement(
        group, slots, fn)

def main(argv):
    net = open_net(FLAGS.input)

    terminal_groups = group_transistor_nets(net)
    conns = get_connections(net)

    clusters = get_clusters(net)

    placement = dict()
    group_membership = []
    placements = []

    cluster_grid = generate_cluster_grid(len(clusters))

    place = functools.partial(place_group, terminal_groups)
    placements = map(place, clusters)

    max_cluster_size = 0
    for group in clusters:
        max_cluster_size = max(max_cluster_size, len(group))
        mask = 0
        for i, terminal_group in enumerate(terminal_groups):
            if any(t in group for t in terminal_group):
                mask |= 1 << i
        group_membership.append(mask)

    subcircuit_assignment = dict()

    attraction = []
    subcircuit_ids = [str(i) for i in range(len(clusters))]

    for i in range(len(clusters)):
        row = []
        for j in range(len(clusters)):
            if j == i:
                row.append(0)
                continue
            row.append((group_membership[i] & group_membership[j]).bit_count())
        attraction.append(row)

    def subcircuit_assignment_cost(placement: dict[str, Slot]):
        cost = 0
        for i, a in enumerate(subcircuit_ids):
            ix, iy = to_xy(placement[a])
            for b in subcircuit_ids[i+1:]:
                jx, jy = to_xy(placement[b])
                cost += math.sqrt((jx-ix) ** 2 + (jy-iy) ** 2) * attraction[i][j]
        return cost


    subcircuit_placement = optimize_placement(subcircuit_ids, cluster_grid, subcircuit_assignment_cost)

    dia = cluster_diameter(max_cluster_size)
    v_out = ((dia + 1) // 2, dia // 2)
    u_out = (-(dia // 2), dia)
    for k in subcircuit_ids:
        u, v = subcircuit_placement[k]
        subcircuit_placement[k] = u_out[0] * u + v_out[0] * v, u_out[1] * u + v_out[1] * v

    def get_placement():
        used_slots = set()

        placement = dict()
        for i, group in zip(subcircuit_ids, placements):
            cu, cv = subcircuit_placement[i]
            for k, (u, v) in group.items():
                pos = Slot(cu + u, cv + v)
                if pos in used_slots:
                    return None
                used_slots.add(pos)
                placement[k] = pos
        return placement

    placement = get_placement()
    assert(placement)

    # The supergrid is slightly slanted, so correct for that by adjusting the angle.
    if len(subcircuit_ids) > 1:
        global uv_base_angle
        uv_base_angle = math.degrees(math.atan2(*to_xy(u_out)))

    kicad = KiCad()
    board = kicad.get_board()
    footprints = board.get_footprints()

    for footprint in footprints:
        ref = footprint.reference_field.text.value
        if ref[0] == 'Q':
            x, y = to_xy(placement[normalize(ref)])
            footprint.position = Vector2.from_xy_mm(x, y)

    board.update_items(footprints)


if __name__ == '__main__':
    app.run(main)
