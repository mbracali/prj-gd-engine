// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/pc_strings.h"

namespace {

const char* const kPcStrings[][2] = {
    // Shown in normal use on PC.
    {"mc_checking", "Checking the save data for the game. Do not remove the me... I mean... Don't turn off the system."},
    {"load_loading", "Loading saved data... Do not shut down or reset system."},
    {"load_success", "GD-Engine data loaded successfully."},
    {"bootup_create_save_check", "No GD-Engine data was present on the system. GD-Engine uses an Autosave feature. Can we create a new save file now?"},
    {"bootup_create_save", "Creating GD-Engine save file. Do not shut down or restart the system."},
    {"bootup_create_success", "GD-Engine data created successfully! Let's rock!"},
    {"bootup_caution", "Caution! GD-Engine uses an Autosave feature.\nWe could not find a safe way to save files, continue without saving?"},
    {"autosave_disable_check", "Do you want to disable the Autosave feature?"},
    {"save_saving", "Saving data... Do not shut down or reset system."},
    {"save_success", "Save successful!"},
    {"mc_save_confirm", "WARNING: Saving will overwrite any existing GD-Engine save data. Are you sure you want to save?"},
    {"mc_load_confirm", "WARNING: If you load data, you will lose your current game progress. Continue with load?"},
    {"mc_new_profile_warning", "WARNING: You have unsaved data. If you continue you will lose your progress."},
    {"pre_delete_confirm", "Before deleting a band, your current progress must be saved. Any existing Guitar Hero data may be overwritten."},
    {"delete_confirm", "Are you sure you want to permanently delete this band?"},
    {"BAND_NAME_EXPLAIN", "This is also your SAVE GAME name."},
    {"HOW_TO_EDIT", "You can edit this name later from Data Settings in the Options menu."},
    // Only when saving fails on PC.
    {"save_error", "SAVE ERROR: Data was not saved successfully. Retry or Continue without saving?"},
    {"mc_nodata_error", "No GD-Engine data found. Do you want to create and try loading again?"},
    {"mc_corrupt_error", "LOAD FAILED: Corrupt data detected. Try loading again?"},
    {"bootup_corrupt_data", "LOAD FAILED: Corrupt data detected or no free space on disk. Try loading again?"},
    {"bootup_replace_save_check",
     "SAVE ERROR: GD-Engine save data already exists and we are conflicting. Select REPLACE to create a brand new save file, or LOAD to use that save data."},
    {"file_exists_error", "SAVE ERROR: GD-Engine data already exists. Overwrite previous data?"},
    // PS2 card states (cannot happen on PC).
    {"bootup_nocard", "No data device detected in the system. GD-Engine uses an Autosave feature. Do you want to start the game without saving?"},
    {"bootup_nospace", "Insufficient space. Provide more free space on the disk the game is running from. Continue without saving?"},
    {"bootup_caution_format_cancelled", "Format cancelled. Continue without saving?"},
    {"different_card", "The status of the saved data changed. Proceed with save?"},
    {"no_card_error", "SAVE ERROR: No save device found. Do you wish to continue the game without saving?"},
    {"no_space_error",
     "SAVE ERROR: Insufficient space to save data. Provide more free space on the disk the game is running from. Continue without saving?"},
    {"no_format_warn",
     "SAVE ERROR: Insufficient space to save data. Provide more free space on the disk the game is running from. Continue without saving?"},
    {"unformatted_card",
     "SAVE ERROR: Insufficient space to save data. Provide more free space on the disk the game is running from. Continue without saving?"},
    {"format_card",
     "SAVE ERROR: Insufficient space to save data. Provide more free space on the disk the game is running from. Continue without saving?"},
    {"format_card_error", "SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Continue without saving?"},
    {"format_no_card_error", "SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Select Retry to try again."},
    {"already_formatted_error", "SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Select Retry to try again."},
    {"mc_card_removed_error", "SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Select Retry to try again."},
    {"mc_nocard_error", "SAVE ERROR: Insufficient space to save data, corrupted format or unknown error. Select Retry to try again."},
    // Never used by the game.
    {"bootup_caution_nodata", "Caution! No GD-Engine save data found. Continue without saving?"},
    {"bootup_save_card_removed", "Caution! No GD-Engine save data found. Continue without saving?"},
    {"bootup_space_check_failed", "Caution! Insufficient space to create a save data. Continue without saving?"},
    // The GD-Engine loading tips.
    {"loading_tip30",
     "GD-Engine Devs: This game saved my life back in the day, very dark times where the POWER OF ROCK helped me to not shut down the light "
     "within. GD-Engine is labor of love, giving back to the community what the community gave to me.\n"
     "Thanks for all the heroes at REDOCTANE, HARMONIX, MTV GAMES and ACTIVISION!"},
    {"loading_tip31", "GD-Engine Devs: Do you know that I play drums as good as the drummer in GD-Engine looks like? Pretty bad right?"},
    {"loading_tip32", "GD-Engine Devs: Can you guess how many parts of the code were vibe coded?"},
    {"loading_tip33",
     "GD-Engine Devs: Yeah, I know... You should not leave important decisions to your drummer... But also, keep an eye on the bassist... "
     "I don't trust this guys... Look how GROOVY they are"},
    {"loading_tip34", "GD-Engine Devs: The original source came out in 2005... This project is from 2026... 21 years... I'm old.."},
    {"loading_tip35",
     "GD-Engine Devs: Thanks to my friends who I message non stop during each milestone of this project: Adler, Lucas, Colin, Mauricio, "
     "Geovane, Caio, Joao, Gabriel...\nYOU GUYS ROCK! (Don't block me)"},
};

}  // namespace

const char* const kPcLoadingTips[] = {"loading_tip30", "loading_tip31", "loading_tip32", "loading_tip33", "loading_tip34", "loading_tip35"};
const int kPcLoadingTipCount = int(sizeof kPcLoadingTips / sizeof kPcLoadingTips[0]);

void applyPcStrings(std::map<std::string, std::string>& strings) {
    for (const auto& [token, text] : kPcStrings) {
        strings[token] = text;
        if (std::string ps2 = std::string(token) + "_ps2"; strings.count(ps2)) strings[ps2] = text;
    }
}
