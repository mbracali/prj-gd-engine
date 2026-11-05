// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class Disc;
class FrontEnd;
class Mixer;
namespace qs {
class Vm;
}

// Practice's section menu plays the song from the highlighted section
// (GD04-105): practice_songpreview_monitor runs GH3's own preload_song /
// begin_song / end_song on the song's streams [C scripts
// menu_choose_practice_section.qb, guitar_song.qb]. These are the stream
// natives those scripts call -- PreloadStream, PreloadStreamDone,
// StartPreloadedStream, SetSoundSeekPosition, PauseSound, StopStream -- over
// the app's song mixer while no song is playing (FrontEnd::songPreview).
// A song's own streams stay the native core's (SongScreen); StopStream keeps
// the song hooks' behaviour for them. Call once the song hooks are in.
void addSongPreviewNatives(qs::Vm& vm, FrontEnd& fe, Mixer& mixer, const Disc& disc);
