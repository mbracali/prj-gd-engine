// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
package io.github.mbracali.gd01;

import org.libsdl.app.SDLActivity;

// The SDL activity running libgd01.so (engine/CMakeLists.txt).
public class GD01Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "gd01" };
    }
}
