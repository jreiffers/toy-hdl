from absl import app
from absl import flags
import cpu.format_pb2 as s
from skidl import *

FLAGS = flags.FLAGS

flags.DEFINE_string('input', '', 'Serialized netlist file.')

def main(argv):
    with open(FLAGS.input, "rb") as f:
         net = s.Network()
         net.ParseFromString(f.read())
         print(net)

if __name__ == '__main__':
    app.run(main)
