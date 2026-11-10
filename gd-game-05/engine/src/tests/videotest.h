// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd05 <disc> --videotest`: every video config/gen/videos.dtb names
// (videos/*.bik) is opened and decoded to its last frame, headless; the
// decoded count must equal the file's own frame count. Prints one line per
// video and "videotest: OK" / "FAIL"; returns the exit code.
int runVideoTest(const std::string& dataPath);
