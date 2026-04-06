#ifndef LOWER_GATES_H__
#define LOWER_GATES_H__

#include "absl/types/span.h"
#include "gate_lib.h"
#include "transistor_lib.h"

// Lowers the given gate network to an equivalent transistor network.
// Input IDs are preserved.
Network Lower(GateNetwork& net);

#endif
