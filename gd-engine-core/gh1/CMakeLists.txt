# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_gh1: reading Guitar Hero 1 (gd-game-01, PS2) songs from another game,
# for the additive-songs mod (gd/core/gh1/reader.h). The GH1 readers (ISO 9660 +
# ARK archive, songs.dta, Harmonix MIDI, VGS / PS-ADPCM audio) are promoted here
# from gd-game-01's own source, kept in namespace gd::core::gh1 so a host's own
# identically named types do not clash.
gdcore_area_library(gh1
  src/reader.cpp
  src/data/iso9660.cpp
  src/data/game_data.cpp
  src/data/dta.cpp
  src/data/midi.cpp
  src/data/vgs.cpp
  src/game/song_db.cpp
  src/game/chart.cpp)
target_include_directories(gdcore_gh1 PRIVATE src)
# The GH1 chart reader uses std::popcount (C++20). Only the sources need it; the
# public header (reader.h) stays C++17-clean, so consumers keep their standard.
target_compile_features(gdcore_gh1 PRIVATE cxx_std_20)
# RandomAccessFile and CHD image support (iso9660.cpp reads .chd images).
target_link_libraries(gdcore_gh1 PUBLIC gdcore_platform)
