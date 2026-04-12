from kipy import KiCad
from kipy.geometry import Vector2, Angle
from absl import app
from absl import flags
from collections import namedtuple
import math
import itertools

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')

from util import get_connections, group_transistors, normalize, open_net

Slot = namedtuple('Slot', ['x', 'y'])
TransistorId = str

# Cost of a wire crossing.
CROSSING_COST = 20


def sign(x):
    return -1 if x < 0 else (1 if x > 1 else 0)


# intersection logic adapted from https://www.geeksforgeeks.org/dsa/check-if-two-given-line-segments-intersect/
def lines_intersect(a, b):

    def on_segment(p, q, r):
        return (q[0] <= max(p[0], r[0]) and q[0] >= min(p[0], r[0])
                and q[1] <= max(p[1], r[1]) and q[1] >= min(p[1], r[1]))

    def orientation(p, q, r):
        return sign((q[1] - p[1]) * (r[0] - q[0]) - \
                    (q[0] - p[0]) * (r[1] - q[1]))

    o1 = orientation(a[0], a[1], b[0])
    o2 = orientation(a[0], a[1], b[1])
    o3 = orientation(b[0], b[1], a[0])
    o4 = orientation(b[0], b[1], a[1])

    return (o1 != o2 and o3 != o4) or \
           (o1 == 0 and on_segment(a[0], b[0], a[1])) or \
           (o2 == 0 and on_segment(a[0], b[1], a[1])) or \
           (o3 == 0 and on_segment(b[0], a[0], b[1])) or \
           (o4 == 0 and on_segment(b[0], a[1], b[1]))


assert (lines_intersect(((0, 0), (10, 10)), ((10, 0), (0, 10))))
assert (not lines_intersect(((0, 0), (10, 10)), ((20, 20), (30, 30))))


class Cost:

    def __init__(self, distance, num_intersections):
        self.distance = distance
        self.num_intersections = num_intersections

    def val(self):
        return self.distance + self.num_intersections * CROSSING_COST

    def __lt__(self, other):
        return self.val() < other.val()

    def __repr__(self):
        return f'{self.val()} ({self.distance}, {self.num_intersections})'


def cost(placement: dict[TransistorId, Slot],
         connections: dict[TransistorId, set[TransistorId]]):
    # This is all kind of wrong. Connections aren't really part to part in the way it's represented here -
    # I should be looking at sets of connected transistors instead.
    dist = 0
    segments = []
    for t1, (t1x, t1y) in placement.items():
        for t2 in connections[t1]:
            if t2 not in placement:
                continue

            # TODO: connected to something outside the group -> should be on the edge?
            (t2x, t2y) = placement[t2]
            dist += math.sqrt((t1x - t2x)**2 + (t1y - t2y)**2)

            segments.append(((t1x, t1y), (t2x, t2y)))

    # This intersection logic is also wrong. Many of the intersections are just endpoints being the same.
    num_intersections = 0
    for i, s1 in enumerate(segments):
        for s2 in segments[i + 1:]:
            if lines_intersect(s1, s2):
                num_intersections += 1

    return Cost(dist, num_intersections)


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
    groups = group_transistors(net)
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

        placement |= optimize_placement(
            group, slots, lambda placement: cost(placement, conns))

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
