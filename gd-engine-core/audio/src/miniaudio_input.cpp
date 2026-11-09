// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "miniaudio" capture backend: an ma_device of type capture, f32; with
// InputSpec::nullDevice a context with only miniaudio's null backend.
#include <cstring>
#include <string>
#include <vector>

#include "input_backends.h"
#include "miniaudio.h"

namespace gd::core::audio::detail {
namespace {

class MiniaudioInput final : public Input {
public:
    ~MiniaudioInput() override { close(); }

    bool open(const InputSpec& want, PushCallback push, std::string& error) override {
        close();
        push_ = std::move(push);
        spec_ = want;
        ma_context_config cc = ma_context_config_init();
        const ma_backend nullBackend[] = {ma_backend_null};
        if ((want.nullDevice ? ma_context_init(nullBackend, 1, &cc, &context_) : ma_context_init(nullptr, 0, &cc, &context_)) != MA_SUCCESS) {
            error = "miniaudio: no capture context";
            return false;
        }
        haveContext_ = true;
        ma_device_config dc = ma_device_config_init(ma_device_type_capture);
        dc.capture.format = ma_format_f32;
        dc.capture.channels = ma_uint32(want.channels);
        dc.sampleRate = ma_uint32(want.sampleRate);
        dc.periodSizeInFrames = want.framesHint > 0 ? ma_uint32(want.framesHint) : 0;
        dc.dataCallback = &MiniaudioInput::onData;
        dc.pUserData = this;
        if (ma_device_init(&context_, &dc, &device_) != MA_SUCCESS) {
            error = "miniaudio: cannot open the recording device";
            close();
            return false;
        }
        haveDevice_ = true;
        spec_.sampleRate = int(device_.sampleRate);
        char devName[256] = {};
        ma_device_get_name(&device_, ma_device_type_capture, devName, sizeof devName, nullptr);
        name_ = std::string("miniaudio ") + MA_VERSION_STRING + " (" + ma_get_backend_name(context_.backend) + ": " + devName + ")";
        return true;
    }
    bool start(std::string& error) override {
        if (!haveDevice_ || ma_device_start(&device_) != MA_SUCCESS) {
            error = "miniaudio: cannot start capture";
            return false;
        }
        return true;
    }
    void pause() override {
        if (haveDevice_) ma_device_stop(&device_);
    }
    void close() override {
        if (haveDevice_) ma_device_uninit(&device_);
        if (haveContext_) ma_context_uninit(&context_);
        haveDevice_ = haveContext_ = false;
    }
    InputSpec spec() const override { return spec_; }
    double latencyMs() const override {
        return haveDevice_ && device_.sampleRate ? 1000.0 * device_.capture.internalPeriodSizeInFrames / device_.sampleRate : 0;
    }
    const char* id() const override { return "miniaudio"; }
    std::string name() const override { return name_; }

private:
    static void onData(ma_device* d, void* /*out*/, const void* in, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioInput*>(d->pUserData);
        if (self->push_ && in) self->push_(static_cast<const float*>(in), int(frames));
    }

    ma_context context_{};
    ma_device device_{};
    bool haveContext_ = false, haveDevice_ = false;
    PushCallback push_;
    InputSpec spec_;
    std::string name_;
};

std::vector<InputDevice> miniaudioDevices() {
    std::vector<InputDevice> out;
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return out;
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&ctx, nullptr, nullptr, &infos, &count) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < count; ++i) out.push_back({infos[i].name, infos[i].name});
    ma_context_uninit(&ctx);
    return out;
}

}  // namespace

InputBackend miniaudioInputBackend() {
    return {"miniaudio", "miniaudio", [](std::string&) { return true; }, [] { return std::unique_ptr<Input>(new MiniaudioInput); },
            miniaudioDevices};
}

}  // namespace gd::core::audio::detail
