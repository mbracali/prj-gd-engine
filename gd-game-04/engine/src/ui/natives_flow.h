// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "data/qb.h"

namespace qs {
class Vm;
}

// What the flow natives keep (natives_flow.cpp).
struct FlowState {
    bool paused = false;
    bool saveExists = false;
    double timerMs = 0;
    struct Handler {
        uint32_t event = 0, script = 0;
        qb::Struct params;
    };
    std::vector<Handler> handlers;
    uint64_t broadcasts = 0;
    uint32_t lastBroadcast = 0;
    std::function<void(uint32_t)> onBroadcast;
};

void addFlowNatives(qs::Vm& vm, FlowState& state);
