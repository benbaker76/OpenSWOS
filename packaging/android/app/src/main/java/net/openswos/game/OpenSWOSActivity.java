package net.openswos.game;

import org.libsdl.app.SDLActivity;

/* SDL's activity, pointed at the game (docs/MOBILE.md): libmain.so is the game, built by
 * packaging/mobile/CMakeLists.txt from the same sources as every other target. */
public class OpenSWOSActivity extends SDLActivity {

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
