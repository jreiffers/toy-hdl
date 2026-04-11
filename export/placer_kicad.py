from kipy import KiCad
from kipy.geometry import Vector2, Angle
from absl import app
from absl import flags

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')

from util import group_transistors, open_net


def main(argv):
    net = open_net(FLAGS.input)
    groups = group_transistors(net)
    conns = dict()

    def normalize(s):
        if '.' in s:
            return s.split('.')[0]
        if s[0] == 'Q':
            return s[1:]
        return s

    for c in net.connections:
        a, b = normalize(c.node_a), normalize(c.node_b)

        if a not in conns:
            conns[a] = set()
        if b not in conns:
            conns[b] = set()

        conns[a].add(b)
        conns[b].add(a)

    def is_connected(a, b):
        a = normalize(a)
        b = normalize(b)
        return b in conns[a]

    rows_cols = dict()

    def distribute_transistors(group, row=0, depth=0):
        for (name, subgroup) in group["children"].items():
            row = distribute_transistors(subgroup, row, depth + 1)

        c0 = depth
        c1 = depth
        c2 = depth

        for id, t in group["transistors"].items():
            tid = f'Q{id}'
            if is_connected(tid, 'vdd'):
                rows_cols[tid] = (row, c0)
                c0 += 1
            elif is_connected(tid, 'vss'):
                rows_cols[tid] = (row + 2, c2)
                c2 += 1
            else:
                rows_cols[tid] = (row + 1, c1)
                c1 += 1

        return row + 3

    distribute_transistors(groups)

    kicad = KiCad()
    board = kicad.get_board()
    footprints = board.get_footprints()

    for footprint in footprints:
        ref = footprint.reference_field.text.value
        if ref[0] == 'Q':
            row, col = rows_cols[ref]
            footprint.position = Vector2.from_xy_mm(col * 5, row * 5)

    board.update_items(footprints)


if __name__ == '__main__':
    app.run(main)
