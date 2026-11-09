// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The miniaudio backend: a ma_device in playback mode, f32 at the asked
// format (miniaudio converts to the device's own), its data callback calling
// the game's pull. The null device is miniaudio's null backend (a thread that
// consumes the frames in real time).
#include <algorithm>
#include <cstdio>

#include "backends.h"
#include "miniaudio.h"

namespace gd::core::audio::detail {
namespace {

class MiniaudioOutput final : public Output {
public:
    ~MiniaudioOutput() override { close(); }

    bool open(const Spec& want, PullCallback pull, std::string& error) override {
        close();
        if (!pull) {
            error = "miniaudio: no pull callback";
            return false;
        }
        pull_ = std::move(pull);
        ma_context_config cc = ma_context_config_init();
        const ma_backend nullBackend[] = {ma_backend_null};
        ma_result r = want.nullDevice ? ma_context_init(nullBackend, 1, &cc, &context_) : ma_context_init(nullptr, 0, &cc, &context_);
        if (r != MA_SUCCESS) {
            error = std::string("miniaudio: no audio backend (") + ma_result_description(r) + ")";
            return false;
        }
        contextUp_ = true;

        ma_device_config dc = ma_device_config_init(ma_device_type_playback);
        dc.playback.format = ma_format_f32;
        dc.playback.channels = ma_uint32(std::clamp(want.channels, 1, int(MA_MAX_CHANNELS)));
        dc.sampleRate = ma_uint32(std::max(want.sampleRate, 8000));
        dc.periodSizeInFrames = want.framesHint > 0 ? ma_uint32(want.framesHint) : 0;
        dc.dataCallback = &MiniaudioOutput::callback;
        dc.pUserData = this;
        // The game asked for this rate and channel count: miniaudio converts
        // to the device's (its resampler: linear, as SDL's default quality).
        r = ma_device_init(&context_, &dc, &device_);
        if (r != MA_SUCCESS) {
            error = std::string("miniaudio: cannot open the playback device (") + ma_result_description(r) + ")";
            close();
            return false;
        }
        deviceUp_ = true;
        spec_.sampleRate = int(device_.sampleRate);
        spec_.channels = int(device_.playback.channels);
        spec_.framesHint = int(device_.playback.internalPeriodSizeInFrames);
        // [I] Heard after the device's whole buffer (periods x period), at
        // the device's own rate.
        const double rate = device_.playback.internalSampleRate ? double(device_.playback.internalSampleRate) : double(spec_.sampleRate);
        latencyMs_ = std::min(250.0, double(device_.playback.internalPeriodSizeInFrames) * double(std::max<ma_uint32>(1, device_.playback.internalPeriods)) * 1000.0 / rate);
        char devName[256] = {};
        ma_device_get_name(&device_, ma_device_type_playback, devName, sizeof devName, nullptr);
        name_ = std::string("miniaudio ") + MA_VERSION_STRING + " (" + ma_get_backend_name(context_.backend) + ": " + devName + ")";
        return true;
    }

    bool start(std::string& error) override {
        if (!deviceUp_) {
            error = "miniaudio: not open";
            return false;
        }
        ma_result r = ma_device_start(&device_);
        if (r != MA_SUCCESS) {
            error = std::string("miniaudio: cannot start (") + ma_result_description(r) + ")";
            return false;
        }
        return true;
    }

    void pause() override {
        if (deviceUp_) ma_device_stop(&device_);  // returns once the callback has stopped
    }

    void close() override {
        if (deviceUp_) ma_device_uninit(&device_);
        deviceUp_ = false;
        if (contextUp_) ma_context_uninit(&context_);
        contextUp_ = false;
    }

    Spec spec() const override { return spec_; }
    double latencyMs() const override { return latencyMs_; }
    const char* id() const override { return "miniaudio"; }
    std::string name() const override { return name_; }

private:
    static void callback(ma_device* device, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioOutput*>(device->pUserData);
        self->pull_(static_cast<float*>(out), int(frames));
    }

    ma_context context_{};
    ma_device device_{};
    bool contextUp_ = false, deviceUp_ = false;
    PullCallback pull_;
    Spec spec_;
    double latencyMs_ = 0;
    std::string name_;
};

}  // namespace

Backend miniaudioBackend() {
    Backend b;
    b.id = "miniaudio";
    b.displayName = "miniaudio";
    // Compiled in: every platform miniaudio supports has a backend (or the
    // null one); a missing device shows at open().
    b.probe = [](std::string&) { return true; };
    b.make = [] { return std::unique_ptr<Output>(new MiniaudioOutput()); };
    return b;
}

}  // namespace gd::core::audio::detail
