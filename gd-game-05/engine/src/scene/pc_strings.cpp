// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The port's PC wording (GD02-154): GH2's strings that name the Xbox 360,
// Xbox Live, consoles, storage devices or sign-in, rewritten for PC as
// gd-game-01 does (gd-game-01/engine/src/ui/pc_strings.cpp), in each of GH2's
// languages. Plus the GD-Engine loading tips, which join the tip pool.
#include "scene/pc_strings.h"

#include <cstring>

namespace scene {
namespace {

struct PcString {
    const char* token;
    const char* text[5];  // eng fre deu ita esl
};

const PcString kPcStrings[] = {
    // Rock Band (gd05, wave 02): its strings that name the Xbox 360, Xbox
    // LIVE, a console or a storage device, for PC (English: the disc's only
    // language). The Rock Central upload errors are never shown in the port
    // (the uploads are answered at once, DECISIONS D8).
    {"error_cheating", {"Sorry, you have a cheat activated, so your scores will not be saved."}},
    {"error_ethernet_unplugged", {"You have lost the network connection. Online features are not available in GD-Engine."}},
    {"error_lost_connection", {"You have lost the network connection. Online features are not available in GD-Engine."}},
    {"game_online_tour", {"Online Tour"}},
    {"how_to_chat", {"Pull to chat (online games only)"}},
    {"how_to_microphone", {"Microphone & Controller"}},
    {"leaderboards_not_available_connection", {"Online leaderboards are not available in GD-Engine."}},
    {"loading_help27", {"If you're singing with a headset or a USB microphone, you can press Confirm on your controller to deploy Overdrive!"}},
    {"loading_help31", {"Handheld microphones are cool and may sound better, but you CAN use any microphone your computer has to sing!"}},
    {"loading_help51", {"Rock Band's songs come from your own disc in GD-Engine."}},
    {"matchmaker_live_enabled", {"ONLINE ENABLED"}},
    {"mc_save_long", {"Saving content. Please don't close the game."}},
    {"mics_changed_desc", {"A microphone has been disconnected! Please check that your microphone works."}},
    {"mm_need_a_profile_hth_net_guest_msg", {"Online play is not available in GD-Engine."}},
    {"mm_need_a_profile_net_msg", {"Online play is not available in GD-Engine."}},
    {"mm_need_a_profile_tour_msg", {"To play in a Tour, you need a profile. GD-Engine keeps yours in its save folder."}},
    {"motd_not_signed_in", {"Online leaderboards are not available in GD-Engine."}},
    {"selsong_storage_changed", {"Your save data has changed. You will now be returned to the main menu."}},
    {"gd_loading_tip1",
     {"GD-Engine Devs: This game saved my life back in the day, very dark times where the POWER OF ROCK helped me to not shut down the light "
      "within. GD-Engine is labor of love, giving back to the community what the community gave to me.\n"
      "Thanks for all the heroes at REDOCTANE, HARMONIX, MTV GAMES and ACTIVISION!"}},
    {"gd_loading_tip2", {"GD-Engine Devs: Do you know that I play drums as good as the drummer in GD-Engine looks like? Pretty bad right?"}},
    {"gd_loading_tip3", {"GD-Engine Devs: Can you guess how many parts of the code were vibe coded?"}},
    {"gd_loading_tip4",
     {"GD-Engine Devs: Yeah, I know... You should not leave important decisions to your drummer... But also, keep an eye on the bassist... "
      "I don't trust this guys... Look how GROOVY they are"}},
    {"gd_loading_tip5", {"GD-Engine Devs: The original game came out in 2006... This project is from 2026... 20 years... I'm old.."}},
    {"gd_loading_tip6",
     {"GD-Engine Devs: Thanks to my friends who I message non stop during each milestone of this project: Adler, Lucas, Colin, Mauricio, "
      "Geovane, Caio, Joao, Gabriel...\nYOU GUYS ROCK! (Don't block me)"}},
};

int languageIndex(const std::string& language) {
    static const char* const kLangs[] = {"eng", "fre", "deu", "ita", "esl"};
    for (int i = 0; i < 5; ++i)
        if (language == kLangs[i]) return i;
    return 0;
}

}  // namespace

const char* const kPcLoadingTips[] = {"gd_loading_tip1", "gd_loading_tip2", "gd_loading_tip3",
                                      "gd_loading_tip4", "gd_loading_tip5", "gd_loading_tip6"};
const int kPcLoadingTipCount = int(sizeof kPcLoadingTips / sizeof kPcLoadingTips[0]);
const char* const kDroppedLoadingTips[] = {"loading_tip10", "loading_tip13"};  // Xbox Live leaderboards
const int kDroppedLoadingTipCount = int(sizeof kDroppedLoadingTips / sizeof kDroppedLoadingTips[0]);

void applyPcStrings(std::map<std::string, std::string>& strings, const std::string& language) {
    const int li = languageIndex(language);
    for (const PcString& s : kPcStrings) {
        const char* text = s.text[li] ? s.text[li] : s.text[0];
        strings[s.token] = text;
    }
}

}  // namespace scene
