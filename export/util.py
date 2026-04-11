import cpu.format_pb2 as s


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

