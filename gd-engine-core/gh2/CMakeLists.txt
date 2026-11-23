# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_gh2: reading Guitar Hero 2 (gd-game-02, Xbox 360) songs from another
# game, for the additive-songs mod (gd/core/gh2/reader.h), mirror of gdcore_gh1.
# The GH2 readers (XDVDFS + ARK archive, songs.dtb, Harmonix MIDI, .mogg Ogg
# Vorbis via stb_vorbis) are promoted here from gd-game-02's source, kept in
# namespace gd::core::gh2 so a host's own identically named types do not clash.
gdcore_area_library(gh2
  src/reader.cpp
  src/data/xdvdfs.cpp
  src/data/random_file.cpp
  src/data/game_data.cpp
  src/data/crypt.cpp
  src/data/dta.cpp
  src/data/midi.cpp
  src/data/mogg.cpp
  src/game/song_db.cpp
  src/game/chart.cpp
  src/audio/vorbis_stream.cpp
  src/third_party/stb_vorbis/stb_vorbis.c)
target_include_directories(gdcore_gh2 PRIVATE src src/third_party/stb_vorbis)
# Third-party stb_vorbis: silence its warnings (not our code).
set_source_files_properties(src/third_party/stb_vorbis/stb_vorbis.c PROPERTIES COMPILE_OPTIONS "-w")
# The chart reader uses std::popcount (C++20); the public header stays C++17.
target_compile_features(gdcore_gh2 PRIVATE cxx_std_20)
# stb_vorbis: no file IO (the mogg is decoded from memory).
target_compile_definitions(gdcore_gh2 PRIVATE STB_VORBIS_NO_STDIO)
target_link_libraries(gdcore_gh2 PUBLIC gdcore_platform)
