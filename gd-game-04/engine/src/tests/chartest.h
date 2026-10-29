// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// --chartest <disc> (docs/band.md "Test"):
//  1. every character, outfit and style (musician_body) and every instrument
//     (musician_instrument) loads, skins (every vertex's bones in the
//     skeleton, every material's textures present) and animates for
//     GD04_CHARTEST_FRAMES (120) frames of a song (GD04_CHARTEST_SONG,
//     default slowride) without a NaN, with its body animations found;
//  2. the band of every venue (or GD04_VENUE): the whole song's poses at 60
//     and at 240 fps agree every 250 ms of song time, and every camera shot
//     with a lookat keeps its member on screen (no band member vanishes);
//  3. golden poses: GD04_CHARTEST_GOLDEN=<file> writes (when missing) or
//     compares a checksum of each venue band's poses at fixed song times;
//  4. GD04_CHARTEST_SHOTS=<dir>: screenshot grids of every body and
//     instrument and of the band in each venue's first shots.
// Headless: SDL_VIDEO_DRIVER=offscreen.
int runCharTest(const std::string& discPath);
