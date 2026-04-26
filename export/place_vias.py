from absl import app
from absl import flags
from kipy import KiCad
from kipy.geometry import Vector2
from kipy.board_types import Via, Track
from kipy.util import from_mm

FLAGS = flags.FLAGS

flags.DEFINE_bool('remove_existing', False, 'Remove existing vias and tracks for VDD/VSS nets. The tool will fail if this is not set and existing vias or tracks exists.')
flags.DEFINE_list('power_nets', ['VSS', 'VDD'], 'List of nets to create vias for.')

def check_existing(board):
    vias = board.get_vias()
    tracks = board.get_tracks()
    
    vias_to_delete = [v for v in vias if v.net.name in FLAGS.power_nets]
    tracks_to_delete = [t for t in tracks if t.net.name in FLAGS.power_nets]
    items_to_delete = vias_to_delete + tracks_to_delete

    if not items_to_delete:
        return

    print (tracks_to_delete)

    assert FLAGS.remove_existing, f"Found {len(vias_to_delete)} existing vias and {len(tracks_to_delete)} existing tracks, but --remove_existing is not set."

    board.remove_items(items_to_delete)


def add_vias_and_tracks(board):
    footprints = board.get_footprints()
    new_items = []
    for f in footprints:
        for p in f.definition.pads:
            if p.net.name not in FLAGS.power_nets:
                continue
            if f.reference_field.text.value[0] != 'Q':
                continue
            v = Via()
            v.position = p.position - Vector2.from_xy_mm(-1, -0.2)
            v.net = p.net
            v.drill_diameter = from_mm(0.2)
            v.diameter = from_mm(0.45)

            new_items.append(v)

            t = Track()
            t.layer = f.layer
            t.net = p.net
            t.start = p.position
            t.end = v.position
            t.width = from_mm(0.16)

            new_items.append(t)

    board.create_items(new_items)

def main(argv):
    kicad = KiCad()
    board = kicad.get_board()

    check_existing(board)
    add_vias_and_tracks(board)


if __name__ == '__main__':
    app.run(main)
