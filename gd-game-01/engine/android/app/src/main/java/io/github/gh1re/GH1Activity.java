package io.github.gh1re;

import org.libsdl.app.SDLActivity;

// The SDL activity running libgh1re.so (engine/CMakeLists.txt).
public class GH1Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "gh1re" };
    }
}
