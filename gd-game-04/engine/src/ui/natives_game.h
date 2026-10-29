// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class Disc;
class FrontEnd;
class TextureCache;

// The natives GH3's front-end scripts call beyond the screen elements:
// input, global tags (the save state), paks, sound and movie requests, the
// platform's dialogs (natives_game.cpp).
void addGameNatives(FrontEnd& fe, const Disc& disc, TextureCache& textures);
