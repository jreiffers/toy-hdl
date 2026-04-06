#ifndef COMPILER_H__
#define COMPILER_H__

#include <functional>

#include "gate_lib.h"
#include "transistor_lib.h"

Network Compile(
    GateNetwork& net, const std::function<void()>& callback = +[]() {});

#endif
