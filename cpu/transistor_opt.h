#ifndef OPT_H__
#define OPT_H__

#include "transistor_lib.h"

// Removes duplicate transistors. Returns true if converged.
bool RunCse(Network& net, int max_iters);

// Removes transistors that aren't connected to anything.
bool RunDte(Network& net);

#endif
