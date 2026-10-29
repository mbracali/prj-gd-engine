// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class GameData;

// Part of --chartest: the band's MidiParsers (char/midi_parser.h). One song
// (hangar18, else the first) runs the hand, fret-position and band echo
// parsers into recording sinks, polled at 60 Hz; the (beat, parser, message)
// log must match the golden line count and FNV-1a hash (GD03_MIDI_LOG=<file>
// writes it); every fret_pos value is a spot_neck_fretNN.mesh, every hand
// clip one of the guitarist fret / strum clips (or a bass flag). Then every
// song's parsers build without errors, and the unknown echo texts per track
// are listed. Prints a summary; returns the failures.
int runMidiParserTest(const GameData& data);
