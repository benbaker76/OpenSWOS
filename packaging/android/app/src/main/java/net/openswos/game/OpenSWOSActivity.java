package net.openswos.game;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;

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

    // Set by the game once it has its data and plays (src/mobile.c): from then on a link from the
    // setup cannot be taken in place -- the game has its data loaded -- so the app asks, and starts
    // again with it (RestartActivity), where the game copies without asking twice ("&ok=1").
    private static volatile boolean sPlaying;

    public static void gamePlaying() {
        sPlaying = true;
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        final Uri data = intent != null ? intent.getData() : null;
        if (data == null || !sPlaying) {
            keep(intent);
            return;
        }
        new AlertDialog.Builder(this)
            .setTitle(getString(R.string.app_name))
            .setMessage("Copy the game's data from the setup? " + getString(R.string.app_name)
                        + " starts again to do it: anything not saved is lost.")
            .setNegativeButton("Not now", null)
            .setPositiveButton("Copy", (d, w) -> {
                Intent again = new Intent(this, RestartActivity.class);
                again.setData(Uri.parse(data.toString() + "&ok=1"));
                again.putExtra(RestartActivity.EXTRA_PID, Process.myPid());
                again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(again);
                Runtime.getRuntime().exit(0);
            })
            .show();
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
