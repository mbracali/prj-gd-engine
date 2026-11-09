// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The OpenAL backend. OpenAL Soft is LGPL, so the core never links it: the
// library ships next to the game module (gd-engine-core/docs/audio.md,
// "Packaging") and is loaded here at run time with SDL_LoadObject. Without it
// the backend reports itself unavailable and the game stays on local.
//
// One device, context and source per Output. The source plays a callback
// buffer (AL_SOFT_callback_buffer): OpenAL Soft's mixer thread asks for bytes
// and the game's pull fills them, so no queue thread is needed. The source
// plays its channels straight to the speakers (AL_SOFT_direct_channels, no
// HRTF, no limiter): a game's own mix passes through unchanged.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "backends.h"

namespace gd::core::audio::detail {
namespace {

// The few OpenAL types and values used (al.h / alc.h / alext.h), declared
// here so the backend needs no OpenAL headers to build.
using ALuint = unsigned int;
using ALint = int;
using ALenum = int;
using ALsizei = int;
using ALboolean = char;
using ALCint = int;
using ALCenum = int;
using ALCboolean = char;
using ALCint64SOFT = int64_t;
struct ALCdevice;
struct ALCcontext;
constexpr ALenum AL_NO_ERROR = 0, AL_BUFFER = 0x1009, AL_TRUE = 1;
constexpr ALenum AL_FORMAT_MONO_FLOAT32 = 0x10010, AL_FORMAT_STEREO_FLOAT32 = 0x10011;
constexpr ALenum AL_DIRECT_CHANNELS_SOFT = 0x1033, AL_VERSION = 0xB002, AL_RENDERER = 0xB003;
constexpr ALCenum ALC_FREQUENCY = 0x1007, ALC_REFRESH = 0x1008, ALC_DEVICE_SPECIFIER = 0x1005, ALC_FALSE = 0;
constexpr ALCenum ALC_HRTF_SOFT = 0x1992, ALC_OUTPUT_LIMITER_SOFT = 0x199A, ALC_DEVICE_LATENCY_SOFT = 0x1600;
constexpr ALCenum ALC_ALL_DEVICES_SPECIFIER = 0x1013;

using BufferCallback = ALsizei(SDLCALL*)(void* user, void* data, ALsizei bytes);

// The library and its functions, loaded once and kept for the process (an
// LGPL library's threads and handlers are not unloaded under a running game).
struct AL {
    SDL_SharedObject* lib = nullptr;
    std::string path, error;
    ALCdevice* (*alcOpenDevice)(const char*) = nullptr;
    ALCboolean (*alcCloseDevice)(ALCdevice*) = nullptr;
    ALCcontext* (*alcCreateContext)(ALCdevice*, const ALCint*) = nullptr;
    void (*alcDestroyContext)(ALCcontext*) = nullptr;
    ALCboolean (*alcMakeContextCurrent)(ALCcontext*) = nullptr;
    ALCboolean (*alcIsExtensionPresent)(ALCdevice*, const char*) = nullptr;
    void* (*alcGetProcAddress)(ALCdevice*, const char*) = nullptr;
    void (*alcGetIntegerv)(ALCdevice*, ALCenum, ALsizei, ALCint*) = nullptr;
    const char* (*alcGetString)(ALCdevice*, ALCenum) = nullptr;
    ALenum (*alcGetError)(ALCdevice*) = nullptr;
    ALboolean (*alIsExtensionPresent)(const char*) = nullptr;
    void* (*alGetProcAddress)(const char*) = nullptr;
    const char* (*alGetString)(ALenum) = nullptr;
    ALenum (*alGetError)() = nullptr;
    void (*alGenSources)(ALsizei, ALuint*) = nullptr;
    void (*alDeleteSources)(ALsizei, const ALuint*) = nullptr;
    void (*alGenBuffers)(ALsizei, ALuint*) = nullptr;
    void (*alDeleteBuffers)(ALsizei, const ALuint*) = nullptr;
    void (*alSourcei)(ALuint, ALenum, ALint) = nullptr;
    void (*alGetSourcei)(ALuint, ALenum, ALint*) = nullptr;
    void (*alSourcePlay)(ALuint) = nullptr;
    void (*alSourcePause)(ALuint) = nullptr;
    void (*alSourceStop)(ALuint) = nullptr;
    // Extensions (per device / context).
    ALCboolean (*alcSetThreadContext)(ALCcontext*) = nullptr;  // ALC_EXT_thread_local_context
    bool checkedThreadContext = false;
};

// The names the library has per platform. It is loaded from the module's own
// folder only (where the packages put the build of the pinned release), never
// from the system's library path, so every player gets the version tested;
// on Android the APK's library folder is the module's. Development aid:
// GDCORE_OPENAL_LIBRARY=<file> loads that file instead.
const char* const kLibNames[] = {
#if defined(_WIN32)
    "OpenAL32.dll",
#elif defined(__APPLE__)
    "libopenal.1.dylib",
#elif defined(__ANDROID__)
    "libopenal.so",
#else
    "libopenal.so.1",
#endif
};

std::mutex& alMutex() {
    static std::mutex m;
    return m;
}

template <typename F>
bool bind(SDL_SharedObject* lib, F& fn, const char* name, std::string& missing) {
    fn = reinterpret_cast<F>(SDL_LoadFunction(lib, name));
    if (!fn) missing = name;
    return fn != nullptr;
}

// Loads the library (once); null + `error` when absent or incomplete.
AL* loadAL(std::string& error) {
    std::lock_guard<std::mutex> lock(alMutex());
    static AL al;
    static bool tried = false;
    if (tried) {
        error = al.error;
        return al.lib ? &al : nullptr;
    }
    tried = true;
    std::string attempts;
    std::vector<std::string> paths;
    if (const char* forced = SDL_getenv("GDCORE_OPENAL_LIBRARY"); forced && *forced) paths.push_back(forced);
    else {
#if defined(__ANDROID__)
        const std::string dir;  // by name: the APK's library folder
#else
        const std::string dir = thisModuleDir();
#endif
#if !defined(__ANDROID__)
        if (dir.empty()) attempts = "the game module's folder is unknown";
        else
#endif
            for (const char* n : kLibNames) paths.push_back(dir + n);
    }
    for (const std::string& path : paths) {
        al.lib = SDL_LoadObject(path.c_str());
        if (al.lib) {
            al.path = path;
            break;
        }
        attempts += (attempts.empty() ? "" : "; ") + std::string(SDL_GetError());
    }
    if (!al.lib) {
        al.error = "the OpenAL Soft library is not installed next to the game (" + attempts + ")";
        error = al.error;
        return nullptr;
    }
    std::string missing;
    bool ok = bind(al.lib, al.alcOpenDevice, "alcOpenDevice", missing) && bind(al.lib, al.alcCloseDevice, "alcCloseDevice", missing) &&
              bind(al.lib, al.alcCreateContext, "alcCreateContext", missing) &&
              bind(al.lib, al.alcDestroyContext, "alcDestroyContext", missing) &&
              bind(al.lib, al.alcMakeContextCurrent, "alcMakeContextCurrent", missing) &&
              bind(al.lib, al.alcIsExtensionPresent, "alcIsExtensionPresent", missing) &&
              bind(al.lib, al.alcGetProcAddress, "alcGetProcAddress", missing) &&
              bind(al.lib, al.alcGetIntegerv, "alcGetIntegerv", missing) && bind(al.lib, al.alcGetString, "alcGetString", missing) &&
              bind(al.lib, al.alcGetError, "alcGetError", missing) &&
              bind(al.lib, al.alIsExtensionPresent, "alIsExtensionPresent", missing) &&
              bind(al.lib, al.alGetProcAddress, "alGetProcAddress", missing) && bind(al.lib, al.alGetString, "alGetString", missing) &&
              bind(al.lib, al.alGetError, "alGetError", missing) && bind(al.lib, al.alGenSources, "alGenSources", missing) &&
              bind(al.lib, al.alDeleteSources, "alDeleteSources", missing) && bind(al.lib, al.alGenBuffers, "alGenBuffers", missing) &&
              bind(al.lib, al.alDeleteBuffers, "alDeleteBuffers", missing) && bind(al.lib, al.alSourcei, "alSourcei", missing) &&
              bind(al.lib, al.alGetSourcei, "alGetSourcei", missing) && bind(al.lib, al.alSourcePlay, "alSourcePlay", missing) &&
              bind(al.lib, al.alSourcePause, "alSourcePause", missing) && bind(al.lib, al.alSourceStop, "alSourceStop", missing);
    if (!ok) {
        al.error = al.path + " is not a usable OpenAL library (no " + missing + ")";
        error = al.error;
        SDL_UnloadObject(al.lib);  // nothing of it ran
        al.lib = nullptr;
        return nullptr;
    }
    // No ALC call here: OpenAL Soft starts (and picks its driver) on the
    // first one, which belongs to open().
    return &al;
}

class OpenALOutput final : public Output {
public:
    ~OpenALOutput() override { close(); }

    bool open(const Spec& want, PullCallback pull, std::string& error) override {
        close();
        if (!pull) {
            error = "OpenAL: no pull callback";
            return false;
        }
        // OpenAL Soft picks its output driver when it first starts: the null
        // one for a headless run (it mixes in real time and drops the
        // result). [C] ALSOFT_DRIVERS is read once per process (alc init).
        if (want.nullDevice) SDL_setenv_unsafe("ALSOFT_DRIVERS", "null", 0);
        std::string why;
        al_ = loadAL(why);
        if (!al_) {
            error = "OpenAL: " + why;
            return false;
        }
        pull_ = std::move(pull);
        channels_ = std::clamp(want.channels, 1, 2);
        device_ = al_->alcOpenDevice(nullptr);
        if (!device_) {
            error = "OpenAL: cannot open the playback device";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(alMutex());
            if (!al_->checkedThreadContext && al_->alcIsExtensionPresent(nullptr, "ALC_EXT_thread_local_context"))
                al_->alcSetThreadContext =
                    reinterpret_cast<decltype(al_->alcSetThreadContext)>(al_->alcGetProcAddress(nullptr, "alcSetThreadContext"));
            al_->checkedThreadContext = true;
        }
        const ALCint attrs[] = {ALC_FREQUENCY, std::max(want.sampleRate, 8000),
                                ALC_REFRESH, want.framesHint > 0 ? std::max(1, std::max(want.sampleRate, 8000) / want.framesHint) : 0,
                                ALC_HRTF_SOFT, ALC_FALSE, ALC_OUTPUT_LIMITER_SOFT, ALC_FALSE, 0};
        // ALC_REFRESH 0 is not valid: drop it when there is no hint.
        ALCint trimmed[sizeof attrs / sizeof attrs[0]];
        size_t n = 0;
        for (size_t i = 0; attrs[i] != 0; i += 2)
            if (!(attrs[i] == ALC_REFRESH && attrs[i + 1] == 0)) trimmed[n++] = attrs[i], trimmed[n++] = attrs[i + 1];
        trimmed[n] = 0;
        context_ = al_->alcCreateContext(device_, trimmed);
        if (!context_) {
            error = "OpenAL: cannot create a context";
            close();
            return false;
        }
        if (!makeCurrent()) {
            error = "OpenAL: cannot make the context current";
            close();
            return false;
        }
        if (!al_->alIsExtensionPresent("AL_SOFT_callback_buffer") || !al_->alIsExtensionPresent("AL_EXT_FLOAT32")) {
            error = std::string("OpenAL: ") + (al_->alGetString(AL_RENDERER) ? al_->alGetString(AL_RENDERER) : "the library") +
                    " lacks AL_SOFT_callback_buffer or AL_EXT_FLOAT32 (OpenAL Soft 1.21 or newer is needed)";
            close();
            return false;
        }
        using BufferCallbackFn = void (*)(ALuint, ALenum, ALsizei, BufferCallback, void*);
        auto alBufferCallbackSOFT = reinterpret_cast<BufferCallbackFn>(al_->alGetProcAddress("alBufferCallbackSOFT"));
        if (!alBufferCallbackSOFT) {
            error = "OpenAL: no alBufferCallbackSOFT";
            close();
            return false;
        }
        ALCint freq = 0, refresh = 0;
        al_->alcGetIntegerv(device_, ALC_FREQUENCY, 1, &freq);
        al_->alcGetIntegerv(device_, ALC_REFRESH, 1, &refresh);
        spec_.sampleRate = freq > 0 ? freq : std::max(want.sampleRate, 8000);
        spec_.channels = channels_;
        spec_.framesHint = refresh > 0 ? spec_.sampleRate / refresh : 0;

        al_->alGetError();
        al_->alGenSources(1, &source_);
        al_->alGenBuffers(1, &buffer_);
        if (al_->alGetError() != AL_NO_ERROR) {
            error = "OpenAL: cannot create a source";
            close();
            return false;
        }
        // The buffer plays at the device's rate: no resampling in OpenAL.
        alBufferCallbackSOFT(buffer_, channels_ == 2 ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_MONO_FLOAT32, spec_.sampleRate,
                             &OpenALOutput::callback, this);
        al_->alSourcei(source_, AL_BUFFER, ALint(buffer_));
        if (al_->alIsExtensionPresent("AL_SOFT_direct_channels")) al_->alSourcei(source_, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
        if (al_->alGetError() != AL_NO_ERROR) {
            error = "OpenAL: cannot set up the callback buffer";
            close();
            return false;
        }
        // The latency: the device's own report (ALC_SOFT_device_clock), else
        // [I] OpenAL Soft's default of three updates.
        latencyMs_ = refresh > 0 ? 3000.0 / refresh : 0;
        using Int64Fn = void (*)(ALCdevice*, ALCenum, ALsizei, ALCint64SOFT*);
        if (al_->alcIsExtensionPresent(device_, "ALC_SOFT_device_clock")) {
            if (auto get64 = reinterpret_cast<Int64Fn>(al_->alcGetProcAddress(device_, "alcGetInteger64vSOFT"))) {
                ALCint64SOFT ns = 0;
                get64(device_, ALC_DEVICE_LATENCY_SOFT, 1, &ns);
                if (ns > 0) latencyMs_ = std::min(250.0, double(ns) / 1e6);
            }
        }
        const char* dev = al_->alcGetString(device_, ALC_ALL_DEVICES_SPECIFIER);
        if (!dev || !*dev) dev = al_->alcGetString(device_, ALC_DEVICE_SPECIFIER);
        const char* ver = al_->alGetString(AL_VERSION);
        name_ = std::string("OpenAL ") + (ver ? ver : "?") + " (" + (dev ? dev : "default device") + "; " + al_->path + ")";
        return true;
    }

    bool start(std::string& error) override {
        if (!source_) {
            error = "OpenAL: not open";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(pullMutex_);
            paused_ = false;
        }
        makeCurrent();
        al_->alSourcePlay(source_);
        if (al_->alGetError() != AL_NO_ERROR) {
            error = "OpenAL: cannot start the source";
            return false;
        }
        return true;
    }

    void pause() override {
        if (!source_) return;
        makeCurrent();
        al_->alSourcePause(source_);
        // The mixer thread may be in the callback: once this lock is held it
        // is out, and later calls play silence.
        std::lock_guard<std::mutex> lock(pullMutex_);
        paused_ = true;
    }

    void close() override {
        if (!al_) return;
        if (context_) {
            makeCurrent();
            if (source_) {
                al_->alSourceStop(source_);
                al_->alDeleteSources(1, &source_);
            }
            if (buffer_) al_->alDeleteBuffers(1, &buffer_);
            source_ = buffer_ = 0;
            if (al_->alcSetThreadContext) al_->alcSetThreadContext(nullptr);
            else al_->alcMakeContextCurrent(nullptr);
            al_->alcDestroyContext(context_);
            context_ = nullptr;
        }
        if (device_) al_->alcCloseDevice(device_);  // [C] joins the mixer thread: no callback after this
        device_ = nullptr;
    }

    Spec spec() const override { return spec_; }
    double latencyMs() const override { return latencyMs_; }
    const char* id() const override { return "openal"; }
    std::string name() const override { return name_; }

private:
    // Each output has its own context: current on this thread only when the
    // library can (OpenAL Soft can), else for the process.
    bool makeCurrent() {
        if (al_->alcSetThreadContext) return al_->alcSetThreadContext(context_) != 0;
        return al_->alcMakeContextCurrent(context_) != 0;
    }

    static ALsizei SDLCALL callback(void* user, void* data, ALsizei bytes) {
        auto* self = static_cast<OpenALOutput*>(user);
        const int frameBytes = self->channels_ * int(sizeof(float));
        const int frames = bytes / frameBytes;
        std::lock_guard<std::mutex> lock(self->pullMutex_);
        if (self->paused_ || frames <= 0) std::memset(data, 0, size_t(bytes));
        else self->pull_(static_cast<float*>(data), frames);
        // Fewer bytes than asked would end the stream: always all of them.
        return bytes;
    }

    AL* al_ = nullptr;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    ALuint source_ = 0, buffer_ = 0;
    int channels_ = 2;
    PullCallback pull_;
    std::mutex pullMutex_;
    bool paused_ = true;
    Spec spec_;
    double latencyMs_ = 0;
    std::string name_;
};

}  // namespace

Backend openalBackend() {
    Backend b;
    b.id = "openal";
    b.displayName = "OpenAL";
    b.probe = [](std::string& why) { return loadAL(why) != nullptr; };
    b.make = [] { return std::unique_ptr<Output>(new OpenALOutput()); };
    return b;
}

}  // namespace gd::core::audio::detail
