// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
package io.github.mbracali.gdul;

import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;

import org.libsdl.app.SDLActivity;

// The SDL activity running the launcher (libmain.so, gd-engine-launcher/CMakeLists.txt).
// The game modules (libgd-game-NN.so) are opened by the launcher itself.
public class GdulActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // A disc image chosen in the file dialog: keep the right to read it after
    // a restart too (the launcher copies it into its own folder meanwhile).
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null) {
            keep(data.getData());
            ClipData clip = data.getClipData();
            for (int i = 0; clip != null && i < clip.getItemCount(); i++) keep(clip.getItemAt(i).getUri());
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void keep(Uri uri) {
        if (uri == null) return;
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            // Not offered by that provider: the copy works without it.
        }
    }
}
