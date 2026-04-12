from kipy import KiCad

from kipy.geometry import Vector2, Angle
from absl import app
from absl import flags
from collections import namedtuple
import math
import itertools
from scipy.spatial import ConvexHull
import numpy as np

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')

from util import get_connections, group_transistors_by_hierarchy, group_transistor_nets, normalize, open_net

Slot = namedtuple('Slot', ['x', 'y'])
TransistorId = str


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
    return np.sqrt(np.sum(diff**2))


score_cache = dict()


def distance_score(placement: dict[TransistorId, Slot],
                   group: set[TransistorId]) -> float:
    pts = []
    for b in group:
        if b not in placement:
            continue
        tx, ty = placement[b]
        pts.append([tx, ty])

    pts_set = frozenset([(x, y) for x, y in pts])
    if pts_set in score_cache:
        return score_cache[pts_set]

    degen_score = degenerate_score(pts)
    if degen_score is not None:
        score = degen_score
    else:
        hull = ConvexHull(points=pts)
        # volume: area, area: perimeter
        score = hull.volume * 2 + hull.area

    score_cache[pts_set] = score
    return score


def cost(placement: dict[TransistorId, Slot],
         nets: dict[TransistorId, list[set[TransistorId]]]):
    dist = 0
    for t1 in placement.keys():
        # TODO: connected to something outside the group -> should be on the edge?
        for g in nets[t1]:
            dist += distance_score(placement, g)

    return Cost(dist)


def swap_two(placement: dict[TransistorId, Slot]):
    copy = placement.copy()
    for a in placement.keys():
        for b in placement.keys():
            if a == b:
                continue
            copy[a], copy[b] = copy[b], copy[a]
            yield copy
            copy[a], copy[b] = copy[b], copy[a]


def use_free_slots(placement: dict[TransistorId, Slot], free_slots):
    copy = placement.copy()

    for a in placement.keys():
        for slot in free_slots:
            orig_slot = copy[a]
            copy[a] = slot
            yield copy
            copy[a] = orig_slot


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
    while cont:
        print(f"Current cost: {current_cost}")
        cont = False
        for candidate in itertools.chain(
                use_free_slots(placement, free_slots()), swap_two(placement)):
            new_cost = cost_fn(candidate)
            if new_cost < current_cost:
                placement = candidate
                current_cost = new_cost
                cont = True
                break

    return placement


def generate_hex_slots(n: int, center_to_center_dist: float,
                       center: Slot) -> list[Slot]:
    d = center_to_center_dist

    def gen_shells():
        yield center
        shell = 1
        to_gen = n
        while to_gen > 0:
            for a in range(6):
                start_rad = math.radians(a * 60)
                start_x = math.cos(start_rad) * shell
                start_y = math.sin(start_rad) * shell

                dir_rad = math.radians((a + 2) * 60)
                dir_x = math.cos(dir_rad)
                dir_y = math.sin(dir_rad)

                for b in range(shell):
                    to_gen -= 1
                    yield Slot((start_x + dir_x * b) * d + center.x,
                               (start_y + dir_y * b) * d + center.y)

            shell += 1

    return list(gen_shells())


def generate_grid_slots(rows: int, cols: int, center_to_center_dist: float,
                        top_left: Slot) -> list[Slot]:
    d = center_to_center_dist
    return [
        Slot(top_left.x + c * d, top_left.y + r * d) for c in range(cols)
        for r in range(rows)
    ]


def main(argv):
    net = open_net(FLAGS.input)

    transistor_nets = group_transistor_nets(net)
    groups = group_transistors_by_hierarchy(net)
    conns = get_connections(net)

    rows_cols = dict()
    flat_groups: list[list[TransistorId]] = []

    def distribute_transistors(group):
        for (name, subgroup) in group["children"].items():
            distribute_transistors(subgroup)

        this_group = [normalize(id) for id in group["transistors"].keys()]
        if this_group:
            flat_groups.append(this_group)

    distribute_transistors(groups)

    placement = dict()
    cont = True
    row = 0

    for group in flat_groups:
        slots = generate_hex_slots(len(group), 5, Slot(0, row * 5))
        print(len(slots), len(group))
        row += 10

        score_cache = dict()
        placement |= optimize_placement(
            group, slots, lambda placement: cost(placement, transistor_nets))

    kicad = KiCad()
    board = kicad.get_board()
    footprints = board.get_footprints()

    for footprint in footprints:
        ref = footprint.reference_field.text.value
        if ref[0] == 'Q':
            x, y = placement[normalize(ref)]
            footprint.position = Vector2.from_xy_mm(x, y)

    board.update_items(footprints)


if __name__ == '__main__':
    app.run(main)
