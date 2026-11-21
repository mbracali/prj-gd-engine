// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// gd24 --inputtest: the input layer with SDL virtual controllers only (real
// devices ignored): a gamepad's buttons give the PSP's, a guitar's frets the
// menus' Cross / Circle and, in a song, the lanes; keyboard keys; the lane
// table; two controls on one key send the action once. Headless
// (SDL_VIDEO_DRIVER=dummy is set); one line per check, exit code 1 on a failure.
int inputTest();
