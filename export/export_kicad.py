import os

if 'KICAD9_SYMBOL_DIR' not in os.environ:
    os.environ['KICAD9_SYMBOL_DIR'] = '/usr/share/kicad/symbols'

from absl import app
from absl import flags
import cpu.format_pb2 as s
from skidl import *
from util import group_transistors_by_hierarchy, open_net, normalize
import re

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')
flags.DEFINE_string('output', '', 'KICAD schematic output file.')
flags.DEFINE_list('jumpers', '', 'Inputs that are set using jumpers.')
flags.DEFINE_float('indicator_led_voltage', 2.4,
                   'Forward voltage of indicator LEDs.')
flags.DEFINE_float('indicator_led_current', 0.004, 'Indicator LED current.')
flags.DEFINE_float('supply_voltage', 5.0, 'VDD')
flags.DEFINE_string(
    'indicator_tag_re', '',
    'Regular expression for tagged nodes to emit indicators for.')

AVAILABLE_RESISTORS = [150, 300, 680, 1200, 2400, 3900, 5600]


def get_indicator_resistor_value():
    ideal = (FLAGS.supply_voltage -
             FLAGS.indicator_led_voltage) / FLAGS.indicator_led_current
    actual = min(AVAILABLE_RESISTORS, key=lambda x: abs(x - ideal))
    assert (ideal / 2 < actual < ideal * 2)
    return actual


def generate_jumper(nets, input):
    vss_conn, out, vdd_conn = Part(
        'Connector_Generic',
        f'Conn_01x{input.bitwidth:02}',
        dest=TEMPLATE,
        footprint=
        f'Connector_PinHeader_2.54mm:PinHeader_1x{input.bitwidth:02}_P2.54mm_Vertical'
    )(3)

    assert out[1] is not None, out
    for i in range(input.bitwidth):
        vss_conn[i + 1] & nets['vss']
        vdd_conn[i + 1] & nets['vdd']
        out[i + 1] & nets[f'{input.name}.{i}']


def generate_indicator(nets, net, tag):
    led = Part('Device',
               'LED',
               dest=TEMPLATE,
               footprint='LED_SMD:LED_0603_1608Metric')()
    led.tag = f'{tag}/led'

    r = Part('Device',
             'R',
             dest=TEMPLATE,
             value=str(get_indicator_resistor_value()),
             footprint='Resistor_SMD:R_0402_1005Metric')()
    r.tag = f'{tag}/r'

    nets[net] & r & led["A K"] & nets['vss']


def generate_header(nets, header_nets):
    conn = Part(
        'Connector_Generic',
        f'Conn_01x{len(header_nets):02}',
        dest=TEMPLATE,
        footprint=
        f'Connector_PinHeader_2.54mm:PinHeader_1x{len(header_nets):02}_P2.54mm_Horizontal'
    )()
    for i, net in enumerate(header_nets):
        nets[net] & conn[i + 1]


def init_skidl_nets(net):
    nets = {}
    nets['vdd'] = Net("VDD")
    nets['vss'] = Net("VSS")

    header_nets = ['vdd', 'vss']
    seen_jumpers = []

    for input in net.inputs:
        print(f'Processing input {input.name}')
        nets[input.name] = Bus(input.name, input.bitwidth)
        for i in range(input.bitwidth):
            nets[f'{input.name}.{i}'] = nets[input.name][i]
        if input.bitwidth == 1:
            nets[input.name] = nets[input.name][0]
        if input.name in FLAGS.jumpers:
            generate_jumper(nets, input)
            seen_jumpers.append(input.name)
        else:
            for i in range(input.bitwidth):
                header_nets.append(f'{input.name}.{i}')

    assert set(seen_jumpers) == set(FLAGS.jumpers), seen_jumpers

    for output in net.outputs:
        bus = Bus(output.name, len(output.terminals))
        for i in range(len(output.terminals)):
            header_nets.append(f'{output.name}.{i}')
            nets[f'{output.name}.{i}'] = bus[i]

    return nets, header_nets


def generate(net):
    n = Part("Transistor_FET",
             "2N7002",
             dest=TEMPLATE,
             footprint='Package_TO_SOT_SMD:SOT-23')
    p = Part("Transistor_FET", "Si2319CDS", dest=TEMPLATE)

    nets, header_nets = init_skidl_nets(net)

    transistors = []

    def emit_transistors(group, path='/'):
        for (name, subgroup) in group["children"].items():
            with SubCircuit(name):
                emit_transistors(subgroup, path + name + '/')
        for id, t in group["transistors"].items():
            # TODO: Chip selection - if src is not vss/vdd, a CD4007 is probably needed.
            if t.kind == s.Transistor.Kind.kNChannel:
                transistor = n()
            else:
                transistor = p()

            assert transistor["G"] is not None, transistor
            assert transistor["S"] is not None, transistor
            assert transistor["D"] is not None, transistor

            transistor.tag = f'{path}/t{id}'
            transistor.ref = f'Q{id}'
            transistors.append(transistor)
            nets[f"{id}.g"] = transistor["G"]
            nets[f"{id}.s"] = transistor["S"]
            nets[f"{id}.d"] = transistor["D"]

    emit_transistors(group_transistors_by_hierarchy(net))

    def get_net(name):
        assert name in nets, f'net {name} not found'
        net = nets[name]
        assert net is not None, f'net {name} is None'
        return net

    for c in net.connections:
        a = c.node_a
        b = c.node_b

        get_net(a) & get_net(b)

    for output in net.outputs:
        for i, t in enumerate(output.terminals):
            nets[f'{output.name}.{i}'] & get_net(t)

    if FLAGS.indicator_tag_re:
        exp = re.compile(FLAGS.indicator_tag_re)
        for tag in net.tagged_nodes:
            tid = normalize(tag.node)
            assert tid[0] in '0123456789'
            full_tag = f'{"/".join(net.transistors[int(tid)].scope)}/{tag.tag}'
            if exp.match(full_tag):
                print(f'Generating indicator {full_tag} for node {tag.node}')
                generate_indicator(nets, tag.node, full_tag)

    generate_header(nets, header_nets)
    generate_netlist(tool=KICAD9, file=FLAGS.output)


def main(argv):
    assert len(FLAGS.input) > 0
    assert len(FLAGS.output) > 0

    print(
        f"Indicator current: {(FLAGS.supply_voltage - FLAGS.indicator_led_voltage) / get_indicator_resistor_value() * 1000} mA"
    )

    generate(open_net(FLAGS.input))


if __name__ == '__main__':
    app.run(main)
