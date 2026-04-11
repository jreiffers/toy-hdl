import cpu.format_pb2 as s

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

