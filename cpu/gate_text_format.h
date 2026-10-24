#ifndef CPU_GATE_TEXT_FORMAT_H_
#define CPU_GATE_TEXT_FORMAT_H_

#include <string>
#include <string_view>

#include "cpu/gate_lib.h"

std::string PrintNetwork(GateNetwork& net);
void ParseNetwork(GateNetwork& net, std::string_view source);

#endif  // CPU_GATE_TEXT_FORMAT_H_
