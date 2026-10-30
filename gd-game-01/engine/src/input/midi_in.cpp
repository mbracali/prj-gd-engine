// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "input/midi_in.h"

#include <SDL3/SDL.h>

#include <map>
#include <mutex>

#if defined(GD_RTMIDI_API) && !defined(__RTMIDI_DUMMY__) && !defined(__ANDROID__)
#define GD_MIDI 1
#include "RtMidi.h"
#endif

struct MidiIn::Impl {
    std::mutex mutex;
    std::vector<Event> queue;  // filled by RtMidi's threads
#ifdef GD_MIDI
    struct Port {
        std::string name;
        Impl* impl = nullptr;
        std::unique_ptr<RtMidiIn> in;
    };
    std::map<std::string, std::unique_ptr<Port>> open;
    std::unique_ptr<RtMidiIn> probe;  // lists the ports

    static void callback(double, std::vector<unsigned char>* msg, void* user) {
        auto* p = static_cast<Port*>(user);
        if (!msg || msg->size() < 3) return;
        const unsigned status = (*msg)[0] & 0xf0u;
        Event e;
        e.port = p->name, e.index = (*msg)[1] & 0x7f, e.value = (*msg)[2] & 0x7f, e.ns = SDL_GetTicksNS();
        if (status == 0x90) e.note = true;
        else if (status == 0x80) e.note = true, e.value = 0;
        else if (status == 0xb0) e.note = false;
        else return;
        std::lock_guard<std::mutex> lock(p->impl->mutex);
        if (p->impl->queue.size() < 4096) p->impl->queue.push_back(e);
    }
#endif
};

MidiIn::MidiIn() : impl_(std::make_unique<Impl>()) {
#ifdef GD_MIDI
    try {
        impl_->probe = std::make_unique<RtMidiIn>();
    } catch (const RtMidiError& e) {
        SDL_Log("MIDI: %s", e.what());
    }
#endif
}

MidiIn::~MidiIn() {
#ifdef GD_MIDI
    for (auto& [name, p] : impl_->open)
        if (p->in) p->in->closePort();
#endif
}

bool MidiIn::available() const {
#ifdef GD_MIDI
    return impl_->probe != nullptr;
#else
    return false;
#endif
}

std::string MidiIn::backend() const {
#if defined(__ANDROID__)
    return "not available on Android yet";
#elif defined(GD_MIDI)
    return available() ? GD_RTMIDI_API : "the MIDI system could not be opened";
#else
    return "not in this build (no MIDI backend)";
#endif
}

void MidiIn::refresh() {
#ifdef GD_MIDI
    if (!impl_->probe) return;
    std::map<std::string, unsigned> now;
    try {
        const unsigned n = impl_->probe->getPortCount();
        for (unsigned i = 0; i < n; ++i) {
            std::string name = impl_->probe->getPortName(i);
            // Our own ports (RtMidi's clients on ALSA) are not instruments.
            // ALSA's "Midi Through" is a loopback, not an instrument.
            if (!name.empty() && name.find("RtMidi") == std::string::npos && name.rfind("Midi Through", 0) != 0 && !now.count(name)) now[name] = i;
        }
    } catch (const RtMidiError& e) {
        SDL_Log("MIDI: %s", e.what());
        return;
    }
    for (auto it = impl_->open.begin(); it != impl_->open.end();) {
        if (now.count(it->first)) {
            ++it;
            continue;
        }
        SDL_Log("MIDI input gone: %s", it->first.c_str());
        if (it->second->in) it->second->in->closePort();
        it = impl_->open.erase(it);
    }
    for (const auto& [name, index] : now) {
        if (impl_->open.count(name)) continue;
        auto p = std::make_unique<Impl::Port>();
        p->name = name, p->impl = impl_.get();
        try {
            p->in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "gd01");
            p->in->ignoreTypes(true, true, true);  // no sysex, timing or active sensing
            p->in->setCallback(&Impl::callback, p.get());
            p->in->openPort(index, "gd01 input");
            SDL_Log("MIDI input: %s", name.c_str());
        } catch (const RtMidiError& e) {
            SDL_Log("MIDI %s: %s", name.c_str(), e.what());
            p->in.reset();
        }
        impl_->open[name] = std::move(p);  // listed even if it could not open (it stays silent)
    }
#endif
}

std::vector<std::string> MidiIn::ports() const {
    std::vector<std::string> v;
#ifdef GD_MIDI
    for (const auto& [name, p] : impl_->open) v.push_back(name);
#endif
    return v;
}

std::vector<MidiIn::Event> MidiIn::take() {
    std::vector<Event> out;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.swap(impl_->queue);
    return out;
}
