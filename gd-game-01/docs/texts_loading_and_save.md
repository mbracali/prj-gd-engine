# Loading and save screens — From → To

The ID is the game's name for each text (from `ghui/eng/gen/locale.dta`).
In the save messages the port already replaces `{SLOT}` with `1` and `{REQ_SPACE}` with `204KB`.

## 1. Save / load dialog (memory card)

### Dialog titles
| ID | From | To |
|---|---|---|
| mc_title_loading | loading... | |
| mc_title_saving | saving... | |
| mc_title_caution | caution | |
| mc_title_error | memory card error | |
| mc_title_nosave | save not found | |
| mc_title_fileerror | save file error | |
| mc_title_deleting | deleting... (not used by the scripts) | |

### Dialog buttons
These are shared with other screens.

| ID | From | To |
|---|---|---|
| yes | yes | |
| no | no | |
| load | load | |
| replace | replace | |
| retry | retry | |
| cancel | cancel | |
| continue | continue | |

### Shown in normal use on PC
| ID | When | From | To |
|---|---|---|---|
| mc_checking | before every load / save / format | Checking status of memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}.  Do not remove the memory card, reset, or switch off console. | |
| load_loading | loading the save at start-up | Loading Guitar Hero data. Do not remove memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}, reset, or switch off the console. | |
| load_success | load finished | Guitar Hero data loaded successfully. | |
| bootup_create_save_check | first start, no save yet | No Guitar Hero save data present on memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Guitar Hero uses an Autosave feature. {REQ_SPACE} of free space is required to save game data. Do you want to create a save file now? | |
| bootup_create_save | creating the save | Creating Guitar Hero save file. Do not remove memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}, reset, or switch off console. | |
| bootup_create_success | save created | Guitar Hero data created successfully. | |
| bootup_caution | answered No to creating a save | Caution! Guitar Hero uses an Autosave feature. Insertion of a memory card (8MB)(for PlayStation®2) beyond this point may cause data to be overwritten. Continue without saving? | |
| autosave_disable_check | turning Autosave off | Do you want to disable the Autosave feature? | |
| save_saving | every save / autosave | Saving Guitar Hero data. Do not remove memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}, reset, or switch off console. | |
| save_success | save finished | Save successful | |
| mc_save_confirm | Options > Save | WARNING: Saving will overwrite any existing Guitar Hero data on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Are you sure you want to save? | |
| mc_load_confirm | Options > Load | WARNING: If you load data from a memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}, you will lose your current game progress. Continue with load? | |
| mc_new_profile_warning | new band with unsaved progress | WARNING: You have unsaved data. If you continue you will lose your progress. | |
| pre_delete_confirm | before deleting a band | Before deleting a band, your current progress must be saved to a memory card (8MB)(for PlayStation®2).  Any existing Guitar Hero data may be overwritten. | |
| delete_confirm | deleting a band | Are you sure you want to permanently delete this band from your memory card (8MB)(for PlayStation®2)? | |
| BAND_NAME_EXPLAIN | Name Your Band screen | This is also your SAVE GAME name | |
| HOW_TO_EDIT | Name Your Band screen | You can edit this name later from Data Settings in the Options menu | |

### Only when saving fails on PC
| ID | From | To |
|---|---|---|
| save_error | SAVE ERROR: Data was not saved successfully to the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Retry or Continue without saving? | |
| mc_nodata_error | No Guitar Hero data on memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Do you wish to insert a memory card (8MB)(for PlayStation®2) containing Guitar Hero data and try loading again? | |
| mc_corrupt_error | LOAD FAILED: Corrupt data detected on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Try loading again? | |
| bootup_corrupt_data | Corrupt data detected on memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. {REQ_SPACE} of free space is required to save game data. Replace the corrupt file and create a new save file? | |
| bootup_replace_save_check | SAVE ERROR: A Guitar Hero save file already exists on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Select REPLACE to create a brand new save file, or LOAD to use that save data. | |
| file_exists_error | SAVE ERROR: Guitar Hero data already exists on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Overwrite previous data? | |

### PS2 card states (cannot happen on PC; listed for completeness)
| ID | From | To |
|---|---|---|
| bootup_nocard | No memory card (8MB)(for PlayStation®2) detected in MEMORY CARD slot {SLOT}. Guitar Hero uses an Autosave feature. {REQ_SPACE} of space is required to save game data. Do you want to start the game without saving? | |
| bootup_nospace | Insufficient space on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Insert a memory card with at least {REQ_SPACE} of free space, or create some space using the console browser. Continue without saving? | |
| bootup_caution_format_cancelled | Format cancelled. Continue without saving? | |
| different_card | The status of the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT} has changed.  Data may be overwritten.  Proceed with save? | |
| no_card_error | SAVE ERROR: No memory card (8MB)(for PlayStation®2) detected in MEMORY CARD slot {SLOT}. Do you wish to continue the game without saving? | |
| no_space_error | SAVE ERROR: Insufficient space on memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. {REQ_SPACE} of free space is required to save data. Do you want to continue without saving? | |
| no_format_warn | SAVE ERROR: Data could not be saved to the unformatted memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}.  Retry or continue without saving? | |
| unformatted_card | The memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT} is unformatted. You must format this memory card to save game data.  Do you wish to format the memory card in MEMORY CARD slot {SLOT}? | |
| format_card | Formatting memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Do not remove your memory card, reset, or switch off the console. | |
| format_card_error | FORMAT ERROR: An error occurred while attempting to format the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. Retry format? | |
| format_no_card_error | FORMAT ERROR: There is no memory card (8MB)(for PlayStation®2) detected in MEMORY CARD slot {SLOT}. Please reinsert the memory card (8MB)(for PlayStation®2) and select retry to continue formatting. | |
| already_formatted_error | FORMAT ERROR: The memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT} has already been formatted! Please reinsert unformatted memory card and select retry. | |
| card_damaged_error | (no text in the game: shows the raw ID) | |
| mc_card_removed_error | LOAD FAILED:  No memory card (8MB)(for PlayStation®2) detected in MEMORY CARD slot {SLOT}. Do you wish to insert a memory card containing Guitar Hero data and reattempt load? | |
| mc_nocard_error | No memory card (8MB)(for PlayStation®2) inserted in MEMORY CARD slot {SLOT}. Do you wish to insert a memory card (8MB)(for PlayStation®2) containing Guitar Hero data and try loading again? | |

### Never used by the game
| ID | From | To |
|---|---|---|
| bootup_caution_nodata | Caution! No Guitar Hero save data found on memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. {REQ_SPACE} of free space is required to save game data. Continue without saving? | |
| bootup_save_card_removed | No memory card (8MB)(for PlayStation®2) inserted in MEMORY CARD slot {SLOT}. Guitar Hero uses an Autosave feature. {REQ_SPACE} of space is required to save game data. Do you want to start the game without saving? | |
| bootup_space_check_failed | Insufficient space on the memory card (8MB)(for PlayStation®2) in MEMORY CARD slot {SLOT}. At least {REQ_SPACE} is needed to save this game data. Do you want to start the game without saving? | |

## 2. Loading screens

| ID | Where | From | To |
|---|---|---|---|
| METALOADING | menu loading screen | LOADING | |
| LOADING | not shown | LOADING.... | |

### Loading tips (one at random on the song loading screen; some only show on certain difficulties)
| ID | From | To |
|---|---|---|
| loading_tip1 | Left-handed and want to flip the guitar around? Go to Options and turn on Lefty Flip | |
| loading_tip2 | Hit 10 notes in a row for a x2 score multiplier. Longer note streaks are rewarded with higher multipliers. | |
| loading_tip3 | Just like a real guitar player, you don't need to release Fret Buttons on lower notes when you play higher notes. | |
| loading_tip4 | Long notes are worth more the longer you hold them. Keep that fret button held down! | |
| loading_tip5 | Take a break to rest your hands. Rocking out is hard work! | |
| loading_tip6 | Wait until you have a x4 multiplier going, and then use Star Power to max out your score. | |
| loading_tip7 | Star Power can be a life saver! If you're deep in the red use that Star Power to win the crowd back. | |
| loading_tip8 | Don't let your drummer handle the money. | |
| loading_tip9 | Don't let your lead singer steal all the attention. Singers are egomaniacs. | |
| loading_tip10 | They don't really want you to play "Freebird." They're just heckling you. | |
| loading_tip11 | TURN IT UP! | |
| loading_tip12 | Don't eat anything that gets tossed onto the stage. | |
| loading_tip13 | Eleven IS louder than ten. | |
| loading_tip14 | Always keep an empty bottle in the van.  You'll see. | |
| loading_tip15 | Don't let the drummer have an "extended solo" unless you really have to go to the bathroom. | |
| loading_tip16 | If anyone insists on wearing a white belt, kick them out of the band. | |
| loading_tip17 | Don't wear your guitar higher than your belt - you're not in The Beatles. | |
| loading_tip18 | Play perfectly for a few notes and you'll get a streak multiplier. Keep the streak going and your score will skyrocket! | |
| loading_tip19 | Hold down the fret buttons early to prepare for a note. When you see the same note twice in a row, keep that fret button held down. | |
| loading_tip20 | Use your pinky to hit the 4th fret button and you won't need to move your hand as much. | |
| loading_tip21 | Save your star power for sections with many notes. You'll get more points and bigger Rock Meter boosts. | |
| loading_tip22 | Chords are worth twice as many points as regular notes! | |
| loading_tip23 | Practice strumming both up and down to nail those super fast passages. | |
| loading_tip24 | If you're having trouble getting through an especially fast solo, check out the Advanced Techniques Tutorial. | |
| loading_tip25 | At some point, you should think about buying a real guitar. | |
| loading_tip26 | Don't let the drummer have a microphone. It'll just end in tears. | |
| loading_tip27 | Use Star Power to drive the crowd wild. Learn how to control this awesome energy in the Star Power Tutorial. | |
| loading_tip28 | Buy a second guitar controller and engage your friends in rampaging head-to-head Guitar battles! | |
| loading_tip29 | Go spend your hard-earned cash at the Unlock Shop. You'll find cool stuff like secret Characters, customized Gibson Guitars, and Bonus Songs. | |

### Song tips (the original builds `loading_tip_<song>` for the song being loaded; the port does not show these yet)
| ID | From | To |
|---|---|---|
| loading_tip_allofthis | Shaimus Singer Phil Beaudreau and Bassist Johannes "Rash" Raasina work on the Harmonix QA team. Phil is gullible, Rash is less so. | |
| loading_tip_behindthemask | A member of the Harmonix QA staff, former C60 frontman Keith Smith, belts out the vocals for Behind the Mask. | |
| loading_tip_breakingwheel | When not working for the Harmonix audio staff, Jeff Allen beats the skins for his band Artillery. | |
| loading_tip_callout | The Acro-brats are the most brattiest band in Boston. Guitarist Daniel Sussman will steal your lunch money. | |
| loading_tip_cavemanrejoice | Traffickers of meaty, aggressive rock, The Bags have successfully avoided fame and fortune for over 20 years. | |
| loading_tip_cheatonthechurch | The WINNERS of the Be A Guitar Hero contest supply you with slippery red-hot slingin' Quality American Dirtcore. All Hail the Graveyard BBQ! | |
| loading_tip_decontrol | Marcus Henderson, guitarist for Drist, pulls out the solos for a bunch of Guitar Hero songs, including "Bark at the Moon" and "Godzilla." | |
| loading_tip_evenrats | Ah, the soothing sounds of The Slip. Accessible Boston Rock. What's not to love? | |
| loading_tip_eureka | Boston's Upper Crust are a tribute to men who wear tights all over the world. | |
| loading_tip_farewellmyth | Made In Mexico is from Providence, Rhode Island, home to H.P. Lovecraft's body. | |
| loading_tip_fireitup | Before Black Label Society, Zakk tore it up as the guitarist for Ozzy Osbourne. | |
| loading_tip_flyonthewall | A band of destiny, Din features two siblings of Boston rock legends, Tribe. | |
| loading_tip_getready2rokk | Fans of previous Harmonix games will recognize synth pop masters Freezepop - bet you didn't expect them in this game! | |
| loading_tip_guitarhero | Monkey Steals the Peach is a Harmonix super group.  It is made up of 3 artists and a producer: Swid Swiderek, Ryan Lesser, Jason Kendall, Daniel Sussman. | |
| loading_tip_hey | Veteran Harmonix programmer Dan Schmidt sings and strums for Honest Bob and the Factory-to-Dealer Incentives. | |
| loading_tip_sailyourshipby | Izzy "Sparks" Maxwell plays bass for Count Zero. He's also a member of the crack Harmonix audio team. | |
| loading_tip_storyofmylove | Venue artist, Scott Sinclair, is the bass player for The Model Sons. | |
