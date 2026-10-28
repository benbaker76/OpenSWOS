package net.openswos;

import android.os.Bundle;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowInsets;
import android.view.WindowManager;
import java.io.*;
import java.util.zip.*;
import org.libsdl.app.SDLActivity;

/** Local handheld test build: unpack prepared data before native startup. */
public class OpenSWOSActivity extends SDLActivity {
    private IOException dataError;
    private GameKeys gameKeys;
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
            gameKeys = new GameKeys(this, mLayout);
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

    // Activity dispatch sees input before SDL's views; the event still reaches SDL.
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (gameKeys != null && fromPad(event.getSource())) gameKeys.padUsed();
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (gameKeys != null && fromPad(event.getSource())) gameKeys.padUsed();
        return super.dispatchGenericMotionEvent(event);
    }

    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        if (gameKeys != null && event.getActionMasked() == MotionEvent.ACTION_DOWN) gameKeys.touched();
        return super.dispatchTouchEvent(event);
    }

    // Gamepad buttons and sticks only: a handheld's Back key is a keyboard source.
    private static boolean fromPad(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    @Override protected void onPause() {
        if (gameKeys != null) gameKeys.hide();
        super.onPause();
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
