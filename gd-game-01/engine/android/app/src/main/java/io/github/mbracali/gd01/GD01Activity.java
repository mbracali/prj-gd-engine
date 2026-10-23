package io.github.mbracali.gd01;

import org.libsdl.app.SDLActivity;

// The SDL activity running libgd01.so (engine/CMakeLists.txt).
public class GD01Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "gd01" };
    }
}
