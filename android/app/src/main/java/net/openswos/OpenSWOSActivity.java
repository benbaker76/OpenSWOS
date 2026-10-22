package net.openswos;

import android.os.Bundle;
import android.os.Build;
import android.util.Log;
import android.view.WindowInsets;
import android.view.WindowManager;
import java.io.*;
import java.util.zip.*;
import org.libsdl.app.SDLActivity;

/** Local handheld test build: unpack prepared data before native startup. */
public class OpenSWOSActivity extends SDLActivity {
    private IOException dataError;
    @Override protected void onCreate(Bundle state) {
        try {
            unpackRuntime();
        } catch (IOException e) {
            Log.e("OpenSWOS", "Cannot unpack game data", e);
            dataError = e;
        }
        super.onCreate(state);
        if (!mBrokenLibraries) {
            // SDL returns focus to the SurfaceView when the IME closes.
            // Android's default focus foreground would tint the entire game.
            mSurface.setDefaultFocusHighlightEnabled(false);
        }
        if (!mBrokenLibraries && Build.VERSION.SDK_INT >= 30) {
            // Android 15's edge-to-edge mode overlays the IME even with
            // adjustResize. Resize the SDL surface through its parent padding.
            getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
            mLayout.setOnApplyWindowInsetsListener((view, insets) -> {
                int bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
                if (view.getPaddingBottom() != bottom)
                    view.setPadding(0, 0, 0, bottom);
                return insets;
            });
            mLayout.requestApplyInsets();
        }
    }

    @Override public void loadLibraries() {
        // SDL's normal startup-error screen also prevents the native thread starting.
        if (dataError != null) throw new UnsatisfiedLinkError("Game data: " + dataError);
        super.loadLibraries();
    }

    @Override protected String[] getArguments() {
        return new String[] { "--fullscreen=desktop" };
    }

    private void unpackRuntime() throws IOException {
        File root = new File(getFilesDir(), "runtime");
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create runtime");
        // Re-extract on APK update, preserving settings and saves outside runtime.
        long version = new File(getApplicationInfo().sourceDir).lastModified();
        File marker = new File(root, ".installed-" + version);
        if (marker.isFile()) return;
        String prefix = root.getCanonicalPath() + File.separator;
        try (ZipInputStream zip = new ZipInputStream(getAssets().open("runtime.zip"))) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                File out = new File(root, entry.getName());
                if (!out.getCanonicalPath().startsWith(prefix)) throw new IOException("Invalid data path");
                if (entry.isDirectory()) {
                    if (!out.isDirectory() && !out.mkdirs()) throw new IOException("Cannot create " + out);
                } else {
                    File parent = out.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
                    try (OutputStream stream = new FileOutputStream(out)) {
                        int n;
                        while ((n = zip.read(buffer)) != -1) stream.write(buffer, 0, n);
                    }
                }
            }
        }
        if (!marker.createNewFile()) throw new IOException("Cannot mark installed data");
    }
}
