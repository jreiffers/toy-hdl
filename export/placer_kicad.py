from kipy import KiCad
from kipy.geometry import Vector2, Angle
from absl import app
from absl import flags

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')
flags.DEFINE_string(
    'dist_mode', 'group',
    'Whether to consider distances within the *group* or *global*ly.')

from util import get_connections, group_transistors, normalize, open_net


def sum_of_wire_lengths_in_group(rows_cols, conns, t):
    t = normalize(t)
    row, col = rows_cols[t]
    ret = 0
    for b in conns[t]:
        if b not in rows_cols:
            continue
        target_row, target_col = rows_cols[b]
        if FLAGS.dist_mode == 'group' and (target_row // 3 != row // 3):
            continue
        ret += (col - target_col)**2 + (row - target_row)**2
    return ret


def main(argv):
    net = open_net(FLAGS.input)
    groups = group_transistors(net)
    conns = get_connections(net)

    def is_connected(a, b):
        a = normalize(a)
        b = normalize(b)
        return b in conns[a]

    rows_cols = dict()
    flat_groups = []
    max_col_per_group = []

    def distribute_transistors(group, row=0, depth=0):
        for (name, subgroup) in group["children"].items():
            row = distribute_transistors(subgroup, row, depth + 1)

        c0 = depth
        c1 = depth
        c2 = depth

        this_group = []
        for id, t in group["transistors"].items():
            tid = normalize(id)
            if is_connected(tid, 'vdd'):
                rows_cols[tid] = (row, c0)
                c0 += 1
            elif is_connected(tid, 'vss'):
                rows_cols[tid] = (row + 2, c2)
                c2 += 1
            else:
                rows_cols[tid] = (row + 1, c1)
                c1 += 1
            this_group.append(tid)

        if this_group:
            flat_groups.append(this_group)
            max_col_per_group.append(max(c0, c1, c2))

        return row + 3

    distribute_transistors(groups)
    used_rows_cols = set(rows_cols.values())

    def total_len():
        res = 0
        for (id, t) in enumerate(net.transistors):
            res += sum_of_wire_lengths_in_group(rows_cols, conns, f'Q{id}')
        return res

    cont = True
    while cont:
        print(f'Total wire length now: {total_len()}')
        cont = False
        for gr, max_col in zip(flat_groups, max_col_per_group):
            for a in gr:
                len_a = sum_of_wire_lengths_in_group(rows_cols, conns, a)

                a_row, a_col = rows_cols[a]
                # See if swapping a to a free slot helps.
                for i in range(max_col):
                    if ((a_row, i) not in used_rows_cols):
                        rows_cols[a] = (a_row, i)
                        new_len_a = sum_of_wire_lengths_in_group(
                            rows_cols, conns, a)
                        if new_len_a < len_a:
                            len_a = new_len_a
                            cont = True
                            used_rows_cols.remove((a_row, a_col))
                            a_col = i
                            used_rows_cols.add((a_row, i))
                        else:
                            rows_cols[a] = (a_row, a_col)

                for b in gr:

                    def len_ab():
                        return sum_of_wire_lengths_in_group(rows_cols, conns, a) + \
                            sum_of_wire_lengths_in_group(rows_cols, conns, b)

                    if a == b:
                        continue

                    l = len_ab()

                    def swap():
                        rows_cols[a], rows_cols[b] = rows_cols[b], rows_cols[a]

                    swap()
                    if len_ab() < l:
                        cont = True
                    else:
                        swap()

    kicad = KiCad()
    board = kicad.get_board()
    footprints = board.get_footprints()

    for footprint in footprints:
        ref = footprint.reference_field.text.value
        if ref[0] == 'Q':
            row, col = rows_cols[normalize(ref)]
            footprint.position = Vector2.from_xy_mm(col * 5, row * 5)

    board.update_items(footprints)


if __name__ == '__main__':
    app.run(main)
