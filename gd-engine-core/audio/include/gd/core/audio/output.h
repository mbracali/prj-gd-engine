// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// gd::core::audio::Output: one playback stream on a sound device, through a
// backend chosen by id ("miniaudio", "openal", "faudio"; see
// gd-engine-core/docs/audio.md). The game owns the mixing: the backend pulls
// interleaved float frames from the game's callback on its own audio thread.
//
// Rules (engine-backend-guidelines.md section 3): plain C++17, no exceptions
// across this interface (every failure is a false return plus an error
// string, so the game can fall back to its own "local" path), no game types.

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gd::core::audio {

// The output format. Samples are always 32-bit float, interleaved.
struct Spec {
    int sampleRate = 48000;  // frames per second
    int channels = 2;        // 1 (mono) or 2 (stereo); backends may support more
    // The frames the game would like per pull (the device period). A hint: a
    // backend uses its own size when it must. 0 = the backend's default.
    int framesHint = 0;
    // A device that plays nothing but consumes frames in real time, for
    // headless runs and tests (miniaudio's null backend, OpenAL Soft's null
    // output, FAudio on SDL's dummy driver). Ignored by spec().
    bool nullDevice = false;
};

// Fills `frames` frames (frames * channels floats) at `out`. Runs on the
// backend's audio thread: no blocking for long, no exceptions. Whatever it
// leaves unwritten plays as whatever it holds: write every sample.
using PullCallback = std::function<void(float* out, int frames)>;

class Output {
public:
    virtual ~Output() = default;
    Output() = default;
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    // Opens the default playback device for `want` (the backend may pick
    // another rate or period; see spec()). The device starts paused: the
    // callback is not called before start(). False + `error` on failure.
    virtual bool open(const Spec& want, PullCallback pull, std::string& error) = 0;
    // Starts (or resumes) pulling. False + `error` on failure.
    virtual bool start(std::string& error) = 0;
    // Stops pulling; the device stays open. Returns once the callback is no
    // longer running.
    virtual void pause() = 0;
    // Stops and releases the device. Safe to call twice, and on an output
    // that never opened. The destructor calls it.
    virtual void close() = 0;

    // The format actually in use (valid after open()).
    virtual Spec spec() const = 0;
    // The time from a frame being pulled to it being heard, in ms, as well as
    // the backend can tell (its buffering; the OS mixer's may come on top).
    virtual double latencyMs() const = 0;
    // The backend's id ("miniaudio", ...).
    virtual const char* id() const = 0;
    // For logs: the backend, its driver and device, e.g. "miniaudio 0.11.25
    // (PulseAudio: Built-in Audio)".
    virtual std::string name() const = 0;
};

// A backend: its id, a quick check that this platform and build can run it
// (compiled in, library present), and a maker.
struct Backend {
    std::string id;
    std::string displayName;  // the text the menus show ("miniaudio", "OpenAL", "FAudio")
    std::function<bool(std::string& why)> probe;
    std::function<std::unique_ptr<Output>()> make;
};

// The backends compiled into this build, in menu order, whether or not they
// can run here.
std::vector<Backend> backends();
// The ids of the backends that can run on this machine now (probe() true).
std::vector<std::string> available();
// Makes the backend `id`'s output (not yet open). nullptr + `error` when the
// id is unknown or its probe fails.
std::unique_ptr<Output> create(const std::string& id, std::string& error);
// Adds a backend (a game's or a platform's own; ids are unique, a second
// registration of an id replaces the first). Call before create().
void registerBackend(Backend backend);

}  // namespace gd::core::audio
