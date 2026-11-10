// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// gd::core::audio::Input: audio capture (a microphone), the counterpart of
// Output. A game opens one input per microphone and gets interleaved float
// frames pushed to it on the backend's audio thread; it does its own pitch
// detection or level metering. Backends live behind a factory keyed by id,
// as the outputs' (engine-backend-guidelines.md rule 3). No exceptions
// cross the interface: failures are false / nullptr plus an error string.
// See ../../../../docs/audio.md "Input (capture)".

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gd::core::audio {

struct InputSpec {
    int sampleRate = 48000;  // frames per second
    int channels = 1;        // 1 (mono, a microphone) or 2
    int framesHint = 0;      // frames per push the game would like (a hint; 0 = the backend's)
    // A device that records nothing but delivers silence in real time
    // (SDL's dummy driver, miniaudio's null backend): headless runs and tests.
    bool nullDevice = false;
    std::string device;      // a device id from inputDevices(), "" = the default recording device
};

// Called on the backend's thread with `frames` interleaved float frames
// (InputSpec::channels per frame), -1..1. Keep it short: copy and return.
using PushCallback = std::function<void(const float* in, int frames)>;

struct InputDevice {
    std::string id;    // what InputSpec::device takes
    std::string name;  // what the menus show ("USB Microphone")
};

class Input {
public:
    virtual ~Input() = default;
    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Opens the device for `want` (the backend may pick another rate; see
    // spec()). It starts paused: nothing is pushed before start().
    virtual bool open(const InputSpec& want, PushCallback push, std::string& error) = 0;
    virtual bool start(std::string& error) = 0;
    // Stops pushing (returns once the callback is no longer running); the device stays open.
    virtual void pause() = 0;
    // Releases the device. Safe twice and on an input that never opened; the destructor calls it.
    virtual void close() = 0;

    virtual InputSpec spec() const = 0;
    // From the sound reaching the device to it being pushed, in ms, as far
    // as the backend can tell (the game adds its own processing).
    virtual double latencyMs() const = 0;
    virtual const char* id() const = 0;
    virtual std::string name() const = 0;  // for logs: backend, driver, device
};

struct InputBackend {
    std::string id;           // "sdl3", "miniaudio"
    std::string displayName;  // "SDL3", "miniaudio"
    std::function<bool(std::string& why)> probe;
    std::function<std::unique_ptr<Input>()> make;
    // The recording devices this backend sees (empty when none or unknown).
    std::function<std::vector<InputDevice>()> devices;
};

// Every capture backend compiled in, in preference order.
std::vector<InputBackend> inputBackends();
// The ids whose probe passes here.
std::vector<std::string> availableInputs();
// A new input of backend `id`, or nullptr with `error` (unknown id, probe failed).
std::unique_ptr<Input> createInput(const std::string& id, std::string& error);
// The recording devices of backend `id` (empty for an unknown id).
std::vector<InputDevice> inputDevices(const std::string& id);
// Adds a game's or platform's own capture backend.
void registerInputBackend(InputBackend backend);

}  // namespace gd::core::audio
