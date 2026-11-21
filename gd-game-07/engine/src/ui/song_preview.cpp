// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/song_preview.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <string>

#include "audio/mixer.h"
#include "audio/sfx.h"
#include "audio/song_stream.h"
#include "audio/sound_buss.h"
#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/natives_sound.h"

// GH3's section-menu preview, as its scripts drive it [C scripts
// practice_songpreview_monitor, preload_song, startpreloadpaused_song,
// SetSeekPosition_Song, waitforpreload_song, begin_song, end_song]:
//  - preload_song: SongLoadFSB, Band_Balance / Guitar_Balance to the song's
//    playback volumes - 1.5 dB over fadeintime (1 s; the monitor faded them
//    to -20 dB in 0.5 s before), PreloadStream '<prefix>_song',
//    '_guitar' (player 1's part), the extra stream ('_rhythm', or '_guitar'
//    for a rhythm player) and '_crowd' (gh3 songs), each -> unique_id;
//    startpreloadpaused_song: StartPreloadedStream <id> startpaused = 1
//    buss = First_Player_Lead_Playback / Band_Playback /
//    Single_Player_Rhythm_Playback pitch = current_speedfactor * 100
//    (create_choose_practice_section_menu's end_practice_song_slomo set it to
//    1); SetSeekPosition_Song: SetSoundSeekPosition <id> position = the
//    section's marker time;
//  - begin_song: PauseSound <id> pause = 0 for each stream;
//  - end_song (the highlight moved, the menu destroyed): StopStream
//    {unique_id = <song_unique_id>} -- a local end_song never has, so the
//    struct is empty, and StopStream with neither a unique_id nor a stream
//    name is StopAudioStreams: every stream stops [C 0x8227E580: unique_id
//    -> 0x82179AE8, an unnamed name -> 0x82179B28, else 0x8223EDE0(1), the
//    body of StopAudioStreams 0x8227E668].
// The port's song mixer plays the streams (SongStream: guitar, rhythm and
// song; the crowd stream is not on the disc's song banks [I]) while no song
// is; a song's start takes the mixer over (Mixer::play resets its speed).
namespace {
uint32_t K(const char* s) { return qbkey::key(s); }

// The first unnamed item, a key included (a stream's name).
const qb::Value* firstUnnamed(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}

qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

struct State {
    // unique_id -> the stream's name (lower-cased, '<prefix>_song' ...).
    std::map<int32_t, std::string> streams;
    // unique_id -> a streamall sample (the menu music: Menu_Music_SE_spawned).
    std::map<int32_t, uint32_t> sfxStreams;
    int32_t nextId = 0x5000;
    int32_t songId = 0;   // the '<prefix>_song' stream's unique_id
    float pitch = 1;      // StartPreloadedStream's pitch / 100 for it
    bool rhythm = false;  // player 1's stream is '_rhythm' (a rhythm player)
};
}  // namespace

void addSongPreviewNatives(qs::Vm& vm, FrontEnd& fe, Mixer& mixer, const Disc& disc) {
    using qs::ScriptRun;
    using qs::Vm;
    auto st = std::make_shared<State>();
    FrontEnd* fp = &fe;
    Mixer* mp = &mixer;
    const Disc* dp = &disc;
    auto stop = [st, fp, mp](const char* why) {
        FrontEnd::SongPreview& pv = fp->songPreview;
        if (pv.active && !fp->songPlaying) mp->stop();
        if (pv.active) SDL_Log("practice preview: %s stopped (%s)", pv.prefix.c_str(), why);
        pv.active = pv.playing = false;
        st->streams.clear(), st->songId = 0;
    };
    fe.songPreview.positionMs = [fp, mp] {
        const FrontEnd::SongPreview& pv = fp->songPreview;
        return pv.active ? pv.startMs + mp->songTimeMs() : -1.0;
    };
    // PreloadStream <name> buss = master -> unique_id: the song's streams
    // (preload_song's only); any other stream (the boss intros', which only
    // start_gem_scroller's preload asks for: the port's start is native) fails
    // as before [C script preload_song: ScriptAssert on a failure].
    vm.addNative("PreloadStream", [st, fp](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        if (!s) return false;
        // A streamall sample (gh4's menu music, Menu_Music_SE_spawned): played
        // by the sound effects at StartPreloadedStream [C script; I: the port's
        // streams are the sound effects' voices].
        if (fp->sfx.has(s->key)) {
            const int32_t id = st->nextId++;
            st->sfxStreams[id] = s->key;
            if (qb::Struct* l = locals(run)) Vm::set(*l, K("unique_id"), Vm::makeInt(id));
            return true;
        }
        if (fp->songPlaying) return false;
        std::string name = qbkey::name(s->key);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        static const char* const tails[] = {"_song", "_guitar", "_rhythm", "_crowd"};
        if (std::none_of(std::begin(tails), std::end(tails), [&](const char* t) { return endsWith(name, t); })) {
            v.logOnce("PreloadStream: not a song stream: " + name);
            return false;
        }
        const int32_t id = st->nextId++;
        st->streams[id] = name;
        if (endsWith(name, "_song")) {
            st->songId = id, st->pitch = 1;
            fp->songPreview.prefix = name.substr(0, name.size() - 5);
            if (endsWith(fp->songPreview.prefix, "_coop")) fp->songPreview.prefix.resize(fp->songPreview.prefix.size() - 5);
        }
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("unique_id"), Vm::makeInt(id));
        return true;
    });
    // PreloadStreamDone: the bank is read when the stream opens (at once).
    vm.addNative("PreloadStreamDone", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    // StartPreloadedStream <id> startpaused = 1 buss pitch: the song stream's
    // pitch (current_speedfactor * 100) is the preview's speed; player 1's
    // stream (First_Player_Lead_Playback) tells its part [C script startpreloadpaused_song].
    vm.addNative("StartPreloadedStream", [st, fp](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* id = firstUnnamed(p);
        if (!id) return true;
        // A streamall sample: vol in dB, pitch in percent (vol = -5 pitch = 100) [C script Menu_Music_SE_spawned].
        if (auto sf = st->sfxStreams.find(int32_t(id->number())); sf != st->sfxStreams.end()) {
            const qb::Value* b = Vm::param(p, K("buss"));
            const float gain = dbGain(Vm::number(Vm::param(p, K("vol")), 0));
            const float ratio = Vm::number(Vm::param(p, K("pitch")), 100) / 100.0f;
            if (const uint32_t voice = fp->sfx.play(sf->second, gain, ratio, b ? b->key : K("default"))) addScriptVoice(fp->sfx, voice, run.name);
            st->sfxStreams.erase(sf);
            return true;
        }
        auto it = st->streams.find(int32_t(id->number()));
        if (it == st->streams.end()) return true;
        if (int32_t(id->number()) == st->songId) st->pitch = Vm::number(Vm::param(p, K("pitch")), 100) / 100.0f;
        if (const qb::Value* b = Vm::param(p, K("buss")); b && b->key == K("First_Player_Lead_Playback")) st->rhythm = endsWith(it->second, "_rhythm");
        return true;
    });
    // SetSoundSeekPosition unique_id position: the song stream's opens there,
    // paused (startpaused), on the busses startpreloadpaused_song names.
    vm.addNative("SetSoundSeekPosition", [st, fp, mp, dp, stop](Vm& v, ScriptRun&, qb::Struct& p) {
        const int32_t id = int32_t(Vm::number(Vm::param(p, K("unique_id")), 0));
        if (id == 0 || id != st->songId || fp->songPlaying) return true;
        FrontEnd::SongPreview& pv = fp->songPreview;
        const double at = std::max(0.0, double(Vm::number(Vm::param(p, K("position")), 0)));
        std::string err;
        const uint64_t t0 = SDL_GetTicksNS();
        auto stream = SongStream::open(*dp, pv.prefix, err);
        if (!stream || (at > 0 && !stream->seek(at))) {
            v.logOnce("practice preview: cannot play " + pv.prefix + ": " + err);
            stop("no stream");
            return true;
        }
        // Six channels: guitar L R, rhythm L R, song L R; player 1's part
        // First_Player_Lead_Playback, the other Single_Player_Rhythm_Playback,
        // the song Band_Playback [C script startpreloadpaused_song].
        std::vector<Mixer::Track> tracks(6);
        const SoundBusses& sb = soundBusses();
        const int p1 = st->rhythm ? 2 : 0;
        for (int c = 0; c < 2; ++c) {
            tracks[size_t(c)].pan = tracks[size_t(2 + c)].pan = tracks[size_t(4 + c)].pan = c ? 1.0f : -1.0f;
            tracks[size_t(p1 + c)].buss = sb.index(K("First_Player_Lead_Playback"));
            tracks[size_t(2 - p1 + c)].buss = sb.index(K("Single_Player_Rhythm_Playback"));
            tracks[size_t(4 + c)].buss = sb.index(K("Band_Playback"));
        }
        fp->setlistPreview.active = false;  // the mixer is the practice preview's now
        mp->play(std::move(stream), std::move(tracks), 0);
        mp->setPaused(true);
        if (st->pitch != 1.0f) mp->setSlomo(st->pitch, 1.0f);
        pv.active = true, pv.playing = false, pv.startMs = at, pv.speed = st->pitch;
        SDL_Log("practice preview: %s from %.0f ms (speed %.2f; opened in %.0f ms)", pv.prefix.c_str(), at, double(st->pitch), double(SDL_GetTicksNS() - t0) / 1e6);
        return true;
    });
    // PauseSound unique_id pause: begin_song's unpause starts it.
    vm.addNative("PauseSound", [st, fp, mp](Vm&, ScriptRun&, qb::Struct& p) {
        const int32_t id = int32_t(Vm::number(Vm::param(p, K("unique_id")), 0));
        FrontEnd::SongPreview& pv = fp->songPreview;
        if (id == 0 || id != st->songId || !pv.active || fp->songPlaying) return true;
        const bool pause = Vm::number(Vm::param(p, K("pause")), 0) != 0;
        mp->setPaused(pause);
        if (!pause && !pv.playing) ++pv.starts;
        pv.playing = !pause;
        return true;
    });
    // gh4's set list preview [C 0x8219DD60 SongPlayPreview filename =
    // <prefix> content_index: 0x820E45E8 stops the last one, opens
    // "<prefix>_preview.fsb" (its key from "<prefix>_preview", 0x820E24F8);
    // 0x820E49F8 plays it once the stream is open, unpaused, on the buss
    // music_setlist (0x4A0087AE), no loop, no fade (its end callback
    // 0x820E3F80 frees the channel); 0x82190010 SongStopPreview stops it;
    // 0x82190020 SongIsPreviewPlaying: the channel still exists. The
    // scripts do the rest: setlist_songpreview_monitor plays it again when
    // it ended, the menus fade the buss (gig_posters_song_focus, 7 s then
    // -100 dB over 1 s) and Menu_Music_Off stops the menu music].
    auto setlistStop = [fp, mp](const char* why) {
        FrontEnd::SetlistPreview& sp = fp->setlistPreview;
        if (!sp.active) return;
        if (!fp->songPlaying && !fp->songPreview.active) mp->stop();
        SDL_Log("setlist preview: %s stopped (%s)", sp.prefix.c_str(), why);
        sp.active = false;
    };
    vm.addNative("SongPlayPreview", [fp, mp, dp, setlistStop](Vm& v, ScriptRun&, qb::Struct& p) {
        setlistStop("SongPlayPreview");
        const qb::Value* f = Vm::param(p, K("filename"));
        if (!f || fp->songPlaying || fp->songPreview.active) return false;
        std::string prefix = f->type == qb::Type::String || f->type == qb::Type::WString ? f->str : qbkey::name(f->key);
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::string err;
        auto stream = SongStream::openPreview(*dp, prefix, err);
        if (!stream) {
            v.logOnce("setlist preview: cannot play " + prefix + ": " + err);
            return false;
        }
        std::vector<Mixer::Track> tracks(6);
        const int buss = soundBusses().index(K("music_setlist"));
        for (int c = 0; c < 6; ++c) tracks[size_t(c)].pan = (c & 1) ? 1.0f : -1.0f, tracks[size_t(c)].buss = buss;
        mp->play(std::move(stream), std::move(tracks), 0);
        FrontEnd::SetlistPreview& sp = fp->setlistPreview;
        sp.active = true, sp.prefix = prefix, ++sp.starts;
        SDL_Log("setlist preview: %s playing", prefix.c_str());
        return true;
    });
    // SongCheckIfDownloaded filename = <prefix>: the preview is downloaded
    // content ("a<prefix>_preview.fsb" in a content folder) [C 0x8218FDD8,
    // 0x820E27E8]; the port has no downloads, so the disc's (an unknown
    // native passed as true: the monitor took the download path and gave up
    // with DownloadContentLost).
    vm.addNative("SongCheckIfDownloaded", [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    vm.addNative("SongStopPreview", [setlistStop](Vm&, ScriptRun&, qb::Struct&) {
        setlistStop("SongStopPreview");
        return true;
    });
    vm.addNative("SongIsPreviewPlaying", [fp, mp](Vm&, ScriptRun&, qb::Struct&) {
        FrontEnd::SetlistPreview& sp = fp->setlistPreview;
        if (sp.active && (fp->songPlaying || !mp->playing() || mp->drained())) {
            if (!fp->songPlaying) mp->stop();
            sp.active = false;
        }
        return sp.active;
    });
    // StopStream: the preview's song stream, or every stream (no unique_id
    // and no name: StopAudioStreams, end_song's); a song's own streams keep
    // the song hooks' StopStream.
    auto prevStop = vm.native(K("StopStream"));
    vm.addNative("StopStream", [st, fp, stop, prevStop](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* uid = Vm::param(p, K("unique_id"));
        const bool all = !uid && !firstUnnamed(p);
        if (fp->songPreview.active && (all || (uid && int32_t(uid->number()) == st->songId))) stop(all ? "StopStream: all streams" : "StopStream");
        return prevStop ? prevStop(v, run, p) : true;
    });
}
