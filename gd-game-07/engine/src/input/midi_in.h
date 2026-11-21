// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Generic MIDI input (universal_mechanisms.md 1.1.4 / 1.2.1): every MIDI
// input port of the computer, opened while it is there, its note and control
// change messages queued for the game thread. Self-contained (only RtMidi
// behind it, third_party/rtmidi) so other games can copy it as it is.
// Unavailable on Android and in builds without a MIDI backend (Linux without
// the ALSA headers): available() is false and no port is listed.
class MidiIn {
public:
    struct Event {
        std::string port;   // the port's name (the device's id in the Gamepad menu)
        bool note = true;   // a note (on / off), else a control change
        int index = 0;      // the note or controller number, 0..127
        int value = 0;      // the velocity (0: note off) or the controller's value, 0..127
        uint64_t ns = 0;    // SDL_GetTicksNS when it arrived
    };
    MidiIn();
    ~MidiIn();
    MidiIn(const MidiIn&) = delete;
    MidiIn& operator=(const MidiIn&) = delete;

    bool available() const;
    // The backend's name ("ALSA", "WinMM", "CoreMIDI"), or why there is none.
    std::string backend() const;
    // Opens ports that appeared and closes those that went (call now and then, e.g. every 2 s).
    void refresh();
    std::vector<std::string> ports() const;
    // The messages since the last call, oldest first.
    std::vector<Event> take();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
