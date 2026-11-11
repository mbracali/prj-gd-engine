// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The capture backends' registry (gd/core/audio/input.h).
#include "gd/core/audio/input.h"

#include <mutex>

#include "input_backends.h"

namespace gd::core::audio {
namespace {

std::mutex& inputMutex() {
    static std::mutex m;
    return m;
}

std::vector<InputBackend>& inputRegistry() {
    static std::vector<InputBackend> r = [] {
        std::vector<InputBackend> v;
        v.push_back(detail::sdlInputBackend());
#if GDCORE_AUDIO_HAS_MINIAUDIO
        v.push_back(detail::miniaudioInputBackend());
#endif
        return v;
    }();
    return r;
}

}  // namespace

std::vector<InputBackend> inputBackends() {
    std::lock_guard<std::mutex> lock(inputMutex());
    return inputRegistry();
}

std::vector<std::string> availableInputs() {
    std::vector<std::string> ids;
    for (const InputBackend& b : inputBackends()) {
        std::string why;
        if (b.probe && b.probe(why)) ids.push_back(b.id);
    }
    return ids;
}

std::unique_ptr<Input> createInput(const std::string& id, std::string& error) {
    for (const InputBackend& b : inputBackends()) {
        if (b.id != id) continue;
        std::string why;
        if (b.probe && !b.probe(why)) {
            error = id + ": " + why;
            return nullptr;
        }
        return b.make();
    }
    error = "no capture backend '" + id + "'";
    return nullptr;
}

std::vector<InputDevice> inputDevices(const std::string& id) {
    for (const InputBackend& b : inputBackends())
        if (b.id == id && b.devices) return b.devices();
    return {};
}

void registerInputBackend(InputBackend backend) {
    std::lock_guard<std::mutex> lock(inputMutex());
    for (InputBackend& b : inputRegistry())
        if (b.id == backend.id) {
            b = std::move(backend);
            return;
        }
    inputRegistry().push_back(std::move(backend));
}

}  // namespace gd::core::audio
