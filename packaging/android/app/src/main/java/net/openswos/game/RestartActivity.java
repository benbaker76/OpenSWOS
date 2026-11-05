package net.openswos.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/* Starts the game again with a link (OpenSWOSActivity.onNewIntent): run in a process of its own
 * (":restart", AndroidManifest.xml), it makes sure the game's process is gone, starts the game's
 * activity afresh with the link, and goes. */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int pid = getIntent().getIntExtra(EXTRA_PID, 0);
        if (pid != 0)
            Process.killProcess(pid);
        Intent game = new Intent(this, OpenSWOSActivity.class);
        game.setData(getIntent().getData());
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(game);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
