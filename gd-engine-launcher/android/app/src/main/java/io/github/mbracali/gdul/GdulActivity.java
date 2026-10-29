// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
package io.github.mbracali.gdul;

import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;

import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

// The SDL activity running the launcher (libmain.so, gd-engine-launcher/CMakeLists.txt).
// The game modules (libgd-game-NN.so) are opened by the launcher itself.
public class GdulActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // A disc image chosen in the file dialog: keep the right to read it after
    // a restart too. The launcher keeps the document's content:// URI and
    // reads the disc where it is (nothing is copied): see openDocument.
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
            // Not offered by that provider: readable until the app is closed.
            Log.w("gdul", "no persistable permission for " + uri + ": " + e);
        }
    }

    private static String sLastError = "";

    // Called from the launcher (platform.cpp, JNI): opens a document the
    // player chose (content:// URI) for reading and hands its file descriptor
    // over (the caller closes it), or -1 (lastError says why). The games read
    // their disc through /proc/self/fd/<fd> (they use fopen).
    public static int openDocument(String uri) {
        try {
            ParcelFileDescriptor pfd = SDL.getContext().getContentResolver().openFileDescriptor(Uri.parse(uri), "r");
            if (pfd == null) {
                sLastError = "the file's app gave nothing to read";
                return -1;
            }
            return pfd.detachFd();
        } catch (SecurityException e) {
            sLastError = "no permission to read it any more (" + e.getMessage() + ")";
        } catch (Exception e) {
            sLastError = e.toString();
        }
        Log.w("gdul", "openDocument " + uri + ": " + sLastError);
        return -1;
    }

    public static String lastError() { return sLastError; }

    // The document's name as the file dialog shows it ("Guitar Hero.iso"), or "".
    public static String documentName(String uri) {
        try (Cursor c = SDL.getContext().getContentResolver().query(Uri.parse(uri),
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String name = c.getString(0);
                if (name != null) return name;
            }
        } catch (Exception e) {
            Log.w("gdul", "documentName " + uri + ": " + e);
        }
        return "";
    }
}
