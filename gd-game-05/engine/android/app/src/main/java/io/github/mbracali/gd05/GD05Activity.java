// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
package io.github.mbracali.gd05;

import org.libsdl.app.SDLActivity;

// The SDL activity running libgd05.so (engine/CMakeLists.txt).
public class GD05Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "gd05" };
    }
}
