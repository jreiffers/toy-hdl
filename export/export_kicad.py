import os

if 'KICAD9_SYMBOL_DIR' not in os.environ:
    os.environ['KICAD9_SYMBOL_DIR'] = '/usr/share/kicad/symbols'

from absl import app
from absl import flags
import cpu.format_pb2 as s
from skidl import *

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')
flags.DEFINE_string('output', '', 'KICAD schematic output file.')
flags.DEFINE_list('jumpers', '', 'Inputs that are set using jumpers.')


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

    for input in net.inputs:
        nets[input.name] = Bus(input.name, input.bitwidth)
        for i in range(input.bitwidth):
            nets[f'{input.name}.{i}'] = nets[input.name][i]
        if input.bitwidth == 1:
            nets[input.name] = nets[input.name][0]
        if input.name in FLAGS.jumpers:
            generate_jumper(nets, input)
        else:
            for i in range(input.bitwidth):
                header_nets.append(f'{input.name}.{i}')

    for output in net.outputs:
        if len(output.terminals) == 1:
            nets[output.name] = Net(output.name)
            header_nets.append(output.name)
        else:
            bus = Bus(output.name, len(output.terminals))
            nets[output.name] = bus
            for i in range(input.bitwidth):
                nets[f'{output.name}.{i}'] = bus[i]
                header_nets.append(f'{output.name}.{i}')

    return nets, header_nets


def group_transistors(net):
    out = dict([("children", dict()), ("transistors", dict())])
    for (id, t) in enumerate(net.transistors):
        dst = out
        for scope in t.scope:
            scope = str(scope)
            if scope not in dst["children"]:
                dst["children"][scope] = dict([("children", dict()), ("transistors", dict())])
            dst = dst["children"][scope]
        dst["transistors"][id] = t
    return out


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
                emit_transistors(subgroup, path+name+'/')
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
            transistors.append(transistor)
            nets[f"{id}.g"] = transistor["G"]
            nets[f"{id}.s"] = transistor["S"]
            nets[f"{id}.d"] = transistor["D"]

    emit_transistors(group_transistors(net))

    vddt = Part("power", "VDD")
    vsst = Part("power", "VSS")

    vddt & nets['vdd']
    vsst & nets['vss']

    def get_net(name):
        assert name in nets, f'net {name} not found'
        net = nets[name]
        assert net is not None, f'net {name} is None'
        return net

    for c in net.connections:
        a = c.node_a
        b = c.node_b

        get_net(a) & get_net(b)

    generate_header(nets, header_nets)
    generate_netlist(tool=KICAD9, file=FLAGS.output)


def main(argv):
    assert len(FLAGS.input) > 0
    assert len(FLAGS.output) > 0

    with open(FLAGS.input, "rb") as f:
        net = s.Network()
        net.ParseFromString(f.read())
        generate(net)


if __name__ == '__main__':
    app.run(main)
