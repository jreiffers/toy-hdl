import disjoint_set
import cpu.format_pb2 as s
from collections import defaultdict


def normalize(s):
    if type(s) is int:
        return str(s)
    if '.' in s:
        return s.split('.')[0]
    if s[0] == 'Q':
        return s[1:]
    return s


def get_connections(net):
    conns = dict()
    for c in net.connections:
        a, b = normalize(c.node_a), normalize(c.node_b)

        if a not in conns:
            conns[a] = set()
        if b not in conns:
            conns[b] = set()

        conns[a].add(b)
        conns[b].add(a)
    return conns


def open_net(filename):
    with open(filename, "rb") as f:
        net = s.Network()
        net.ParseFromString(f.read())
        return net


def group_transistor_nets(net) -> list[set[str]]:
    nets = disjoint_set.DisjointSet()
    for c in net.connections:
        nets.union(c.node_a, c.node_b)

    flat_groups = []

    for group in nets.itersets():
        if ('vss' in group) or ('vdd' in group):
            continue

        transistors = set(
            [normalize(x) for x in group if x[0] in '0123456789'])
        flat_groups.append(transistors)

    return flat_groups


def group_transistors_by_hierarchy(net, max_depth=None):
    out = dict([("children", dict()), ("transistors", dict())])
    for (id, t) in enumerate(net.transistors):
        dst = out
        depth = 0
        for scope in t.scope:
            if depth == max_depth:
                break
            depth += 1
            scope = str(scope)
            if scope not in dst["children"]:
                dst["children"][scope] = dict([("children", dict()),
                                               ("transistors", dict())])
            dst = dst["children"][scope]
        dst["transistors"][id] = t
    return out


def emit_transistors(n, p, group, nets, path='/'):
    for (name, subgroup) in group["children"].items():
        with SubCircuit(name):
            emit_transistors(n, p, subgroup, nets, path + name + '/')
    for id, t in group["transistors"].items():
        if t.kind == s.Transistor.Kind.kNChannel:
            transistor = n()
        else:
            transistor = p()

        assert transistor["G"] is not None, transistor
        assert transistor["S"] is not None, transistor
        assert transistor["D"] is not None, transistor

        transistor.tag = f'{path}/t{id}'
        transistor.ref = f'Q{id}'
        nets[f"{id}.g"] = transistor["G"]
        nets[f"{id}.s"] = transistor["S"]
        nets[f"{id}.d"] = transistor["D"]


def get_gate_types(footprints):
    gate_types = dict()
    for fp in footprints:
        for pad in fp.definition.pads:
            if pad.net.name.count('_') != 4:
                continue

            _, gate, y, x, _ = pad.net.name.split('_')
            gate_types[(int(x), int(y))] = gate

    return gate_types
