package net.openswos.game;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/* SDL's activity, pointed at the game (docs/MOBILE.md): libmain.so is the game, built by
 * packaging/mobile/CMakeLists.txt from the same sources as every other target. */
public class OpenSWOSActivity extends SDLActivity {

    // A link from OpenSWOS Setup's Wi-Fi copy (<scheme>://get?..., include/phone_copy.h) that
    // opened the app, kept until the game takes it (src/mobile.c, over JNI): at the start, or a
    // new one while it waits on its "the game's data" message.
    private static volatile String sLink;

    private static void keep(Intent intent) {
        Uri data = intent != null ? intent.getData() : null;
        if (data != null)
            sLink = data.toString();
    }

    public static String takeLink() {
        String link = sLink;
        sLink = null;
        return link;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        keep(getIntent());
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        keep(intent);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // The game's command line, from the intent, so a test can start it as
    //   am start -n <app id>/net.openswos.game.OpenSWOSActivity --es args "--version"
    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty())
            return new String[0];
        return args.trim().split("\\s+");
    }
}
