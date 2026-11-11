// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The port's PC wording (GD10-039): GH3's strings that name the Xbox 360,
// Xbox LIVE, gamer profiles, storage devices or achievements, rewritten as
// gd-game-01..03 do. Online play, leaderboards and achievements are not in
// the port (GD10-013), so their messages say so.
#include "script/pc_strings.h"

#include <cctype>
#include <string>
#include <unordered_map>

#include "data/qbkey.h"
#include "script/globals.h"

namespace pcstrings {
namespace {

const char* const kNoOnline = "Online play is not available in GD-Engine.";

const std::unordered_map<std::string, std::string>& table() {
    static const std::unordered_map<std::string, std::string> t = {
        // The main menu's online entry and its dialogs.
        {"XBOX LIVE", "ONLINE"},
        {"XBOX Live", "ONLINE"},
        {"Unable to connect to Xbox LIVE. Please check your network connection and settings and try again.", kNoOnline},
        {"The current Gamer Profile is not enabled for Xbox LIVE multiplayer game play.", kNoOnline},
        {"No gamer profile currently signed in. Please sign into an Xbox LIVE multiplayer enabled gamer profile to continue.", kNoOnline},
        {"You must be signed in to Xbox Live.", kNoOnline},
        {"Are you sure you want to leave this game session? Only the player who accepted the game invite will go to Xbox LIVE.",
         "Are you sure you want to leave this game session? Only the player who accepted the game invite will go online."},
        {"Both players need to have the same Downloaded Songs in order to use them in XBOX LIVE multiplayer games.",
         "Both players need to have the same Downloaded Songs in order to use them in online multiplayer games."},
        // Saving: no storage devices on PC.
        {"No storage device selected or found.", "No save location found."},
        {"No storage device selected or found. If no storage device is available, progress will not be saved.",
         "No save location found. If none is available, progress will not be saved."},
        {"SELECT STORAGE DEVICE", "SELECT SAVE LOCATION"},
        // Saving and loading screens (\n: QScript's line break, kept as written).
        {"Checking storage device...\\nPlease do not turn off the Xbox 360 console.", "Checking the save data...\\nPlease do not turn off the system."},
        {"Deleting Content.\\nPlease don't turn off the Xbox 360 console.", "Deleting Content.\\nPlease don't turn off the system."},
        {"Loading Content.\\nPlease don't turn off the Xbox 360 console.", "Loading Content.\\nPlease don't turn off the system."},
        {"Checking for downloadable content. Please don't turn off your Xbox 360 console.",
         "Checking for downloadable content. Please don't turn off the system."},
        {"Overwrite Failed.\\nNo storage device was selected or the in-use storage device is unavailable.",
         "Overwrite Failed.\\nThe save location is unavailable."},
        {"Save Failed.\\nNo storage device was selected or the in-use storage device is unavailable.",
         "Save Failed.\\nThe save location is unavailable."},
        {"You are not signed in to a Xbox 360 gamer profile. You will be unable to save any game content until you sign in.",
         "No save profile is in use. You will be unable to save any game content."},
        {"Xbox LIVE Main Menu", "Online Main Menu"},
        // Cheats: no achievements or leaderboards to turn off.
        {"WARNING: Some active cheats do not work in career modes and online.", "WARNING: Some active cheats do not work in career modes."},
        {"Also, achievement unlocking and leaderboard posts are turned off.", ""},
        {"Also, leaderboard posts are turned off.", ""},
    };
    return t;
}

// The online tip (dropped) and the GD-Engine tips (gd-game-01's, GH3 2007).
const char* const kDroppedTip = "Solo play getting old? Go online and battle some LIVE competition!";
const char* const kGdTips[] = {
    "GD-Engine Devs: This game saved my life back in the day, very dark times where the POWER OF ROCK helped me to not shut down the "
    "light within. GD-Engine is labor of love, giving back to the community what the community gave to me. Thanks for all the heroes "
    "at REDOCTANE, NEVERSOFT, ACTIVISION and HARMONIX!",
    "GD-Engine Devs: Do you know that I play drums as good as the drummer in GD-Engine looks like? Pretty bad right?",
    "GD-Engine Devs: Can you guess how many parts of the code were vibe coded?",
    "GD-Engine Devs: Yeah, I know... You should not leave important decisions to your drummer... But also, keep an eye on the "
    "bassist... I don't trust this guys... Look how GROOVY they are",
    "GD-Engine Devs: The original game came out in 2007... This project is from 2026... 19 years... I'm old..",
    "GD-Engine Devs: Thanks to my friends who I message non stop during each milestone of this project: Adler, Lucas, Colin, "
    "Mauricio, Geovane, Caio, Joao, Gabriel... YOU GUYS ROCK! (Don't block me)",
};

void apply(qb::Value& v, int depth) {
    if (depth > 32) return;
    if (v.type == qb::Type::String || v.type == qb::Type::WString) v.str = pcText(v.str);
    if (v.st)
        for (qb::Item& it : v.st->items) apply(it.value, depth + 1);
    if (v.arr)
        for (qb::Value& x : v.arr->values) apply(x, depth + 1);
}

}  // namespace

namespace {

// gh4's localized strings (qs.pak; GD10-035): whole strings first (the
// text without a leading \L), then the wording rules on what is left.
const std::unordered_map<std::string, std::string>& qsTable() {
    static const std::unordered_map<std::string, std::string> t = {
        {"Xbox LIVE", "ONLINE"},
        {"Xbox LIVE:", "ONLINE:"},
        {"Xbox LIVE Main Menu", "Online Main Menu"},
        {"PlayStation®Network", "ONLINE"},
        {"GAMER CARD", "PROFILE"},
        {"Gamer card", "Profile"},
        {"VIEW GAMER CARD", "VIEW PROFILE"},
        {"Press \\m0 to view GAMER CARD.", "Press \\m0 to view PROFILE."},
        {"gamertag", "player name"},
        {"GAMERTAG", "PLAYER NAME"},
        {"1. Plug Xbox 360 Controller in", "1. Plug in a gamepad"},
        {"Controller not signed in", "Controller not in use"},
        {"Any changes will not be saved. If using multiple controllers, use a signed in controller to edit and save changes.",
         "Any changes will not be saved."},
        {"You are not signed in to a gamer profile. You will be unable to save any game content until you sign in.",
         "No save profile is in use. You will be unable to save any game content."},
        {"You are not signed in to a gamer profile. You will be unable to access any downloadable content until you sign into a gamer profile.",
         "No save profile is in use. You will be unable to access any downloadable content."},
        {"You are signed in as a guest. Guest profiles cannot save their progress or settings. Do you wish to continue without saving?",
         "No save profile is in use. Progress and settings will not be saved. Do you wish to continue without saving?"},
        {"A user sign in change has caused the game to lose ownership of saves and achievements. As a result, the game has restarted.",
         "The save profile changed. As a result, the game has restarted."},
        {"While the Always Drum Fill, Always Slide, or the Auto Kick cheat(s) are enabled you will no longer be able to earn achievements, "
         "earn high scores, or post scores to any leaderboard. Always Drum Fill, Always Slide, and Auto Kick cheats will not function when "
         "playing on Xbox LIVE.",
         "While the Always Drum Fill, Always Slide, or the Auto Kick cheat(s) are enabled you will no longer be able to earn high scores."},
        {"This game saves data automatically at certain points. Do not switch off the power when the HDD access indicator is flashing.",
         "This game saves data automatically at certain points. Do not close the game while it is saving."},
        {"Purchase using Microsoft® points.", kNoOnline},
    };
    return t;
}

void replaceAll(std::string& s, const char* from, const char* to) {
    const size_t n = std::char_traits<char>::length(from), m = std::char_traits<char>::length(to);
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + m)) s.replace(at, n, to);
}

bool containsNoCase(const std::string& s, const char* what) {
    std::string low = s;
    for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
    return low.find(what) != std::string::npos;
}

}  // namespace

std::string pcQsText(const std::string& text) {
    const bool l = text.size() >= 2 && text[0] == '\\' && text[1] == 'L';
    std::string s = l ? text.substr(2) : text;
    auto it = qsTable().find(s);
    if (it != qsTable().end()) return (l ? "\\L" : "") + it->second;
    const std::string before = s;
    // Pads, headsets and the console.
    replaceAll(s, "Xbox 360 Controllers", "gamepads");
    replaceAll(s, "Xbox 360 Controller", "gamepad");
    replaceAll(s, "Xbox 360 Headsets", "headsets");
    replaceAll(s, "Xbox 360 Headset", "headset");
    replaceAll(s, "your Xbox 360 console", "the system");
    replaceAll(s, "the Xbox 360 console", "the system");
    replaceAll(s, "turn off your console", "turn off the system");
    // Saving: no storage devices on PC.
    replaceAll(s, "No storage device was selected or the in-use storage device is unavailable.", "The save location is unavailable.");
    replaceAll(s, "No storage device selected or found. If no storage device is available,", "No save location found. If none is available,");
    replaceAll(s, "without selecting a storage device", "without a save location");
    replaceAll(s, "storage device", "save location");
    replaceAll(s, "STORAGE DEVICE", "SAVE LOCATION");
    replaceAll(s, "Please return to the Xbox Dashboard and delete some existing data", "Please free some disk space");
    // Online play is not in the port: its preferences name it plainly, the rest say so.
    replaceAll(s, "for Xbox LIVE play", "for online play");
    replaceAll(s, "the Xbox LIVE lobby", "the online lobby");
    if (s.find("Xbox LIVE") != std::string::npos || s.find("PlayStation®Network") != std::string::npos ||
        containsNoCase(s, "gamer profile") || s.find("Xbox Dashboard") != std::string::npos ||
        s.find("set your console") != std::string::npos || s.find("signed in to enter the lobby") != std::string::npos)
        s = kNoOnline;
    if (s == before) return text;
    return (l ? "\\L" : "") + s;
}

const std::string& pcText(const std::string& text) {
    const auto& t = table();
    auto it = t.find(text);
    return it == t.end() ? text : it->second;
}

void applyToStruct(qb::Struct& st) {
    for (qb::Item& it : st.items) apply(it.value, 1);
}

void applyToGlobals(Globals& globals) {
    for (uint32_t name : globals.names()) apply(*globals.ensure(name), 0);
    qb::Value* tips = globals.ensure(qbkey::key("loading_screen_tips"));
    if (tips->type != qb::Type::Array || !tips->arr) return;
    auto& vals = tips->arr->values;
    const qb::Type type = vals.empty() ? qb::Type::String : vals.front().type;
    std::erase_if(vals, [](const qb::Value& x) { return x.str == kDroppedTip; });
    for (const char* tip : kGdTips) {
        qb::Value v;
        v.type = type;
        v.str = tip;
        vals.push_back(v);
    }
}

}  // namespace pcstrings
