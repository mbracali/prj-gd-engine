// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher's action log: one line per action, appended to gdul.log in
// the launcher's save folder:
//
//   2026-10-27T14:03:12-03:00 <OS user> <action>
#pragma once

#include <string>

class ActionLog {
public:
    void open(const std::string& path);
    void add(const std::string& action);
    const std::string& path() const { return path_; }

private:
    std::string path_, user_;
};
