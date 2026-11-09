// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Windows: UTF-8 <-> UTF-16 for the wide-character file APIs (private).
#pragma once

#ifdef _WIN32
#include <string>

namespace gd::core::platform::detail {

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

}  // namespace gd::core::platform::detail
#endif
