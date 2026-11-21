// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// The input layer with virtual controllers (an Xbox 360 guitar and an Xbox
// 360 pad): detection, GH2's layouts, Type cycling, controllers.json and the
// report, in a save folder of its own. Returns the number of failed checks.
int runInputChecks();
