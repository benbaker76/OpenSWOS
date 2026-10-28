package net.openswos;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import org.libsdl.app.SDLActivity;

/** Touch access to game keys, including keys missing from Android's IME. */
final class GameKeys {
    private final Context context;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SparseArray<Runnable> releases = new SparseArray<>();
    private final LinearLayout panel;
    private final Button toggle;

    GameKeys(Context context, ViewGroup parent) {
        this.context = context;
        panel = new LinearLayout(context);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(4), dp(4), dp(4), dp(4));
        panel.setBackgroundColor(0x70202038);
        panel.setElevation(dp(8));
        panel.setClickable(true); // Do not pass taps between keys into the game.
        panel.setVisibility(View.GONE);

        LinearLayout row = row();
        key(row, "Esc", KeyEvent.KEYCODE_ESCAPE);
        for (int i = 0; i < 10; i++) key(row, "F" + (i + 1), KeyEvent.KEYCODE_F1 + i);
        row = row();
        for (char c : "1234567890".toCharArray()) key(row, "" + c, KeyEvent.KEYCODE_0 + c - '0');
        key(row, "Bksp", KeyEvent.KEYCODE_DEL);
        letters("QWERTYUIOP");
        row = letters("ASDFGHJKL");
        key(row, "Enter", KeyEvent.KEYCODE_ENTER);
        row = letters("ZXCVBNM");
        key(row, ",", KeyEvent.KEYCODE_COMMA);
        key(row, ".", KeyEvent.KEYCODE_PERIOD);
        key(row, "/", KeyEvent.KEYCODE_SLASH);
        row = row();
        key(row, "Tab", KeyEvent.KEYCODE_TAB);
        key(row, "Space", KeyEvent.KEYCODE_SPACE);
        key(row, "\u2190", KeyEvent.KEYCODE_DPAD_LEFT);
        key(row, "\u2193", KeyEvent.KEYCODE_DPAD_DOWN);
        key(row, "\u2191", KeyEvent.KEYCODE_DPAD_UP);
        key(row, "\u2192", KeyEvent.KEYCODE_DPAD_RIGHT);
        Button hide = button("Hide");
        hide.setOnClickListener(v -> hide());
        row.addView(hide, cell());

        RelativeLayout.LayoutParams bounds = new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.MATCH_PARENT, dp(288));
        bounds.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        bounds.addRule(RelativeLayout.CENTER_HORIZONTAL);
        bounds.setMargins(dp(8), 0, dp(8), dp(8));
        parent.addView(panel, bounds);
        parent.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            int available = b - t - parent.getPaddingBottom();
            int height = Math.max(1, Math.min(dp(288), available - dp(64)));
            int width = Math.max(1, Math.min(dp(760), r - l - dp(16)));
            if (bounds.height != height || bounds.width != width) {
                bounds.height = height;
                bounds.width = width;
                panel.setLayoutParams(bounds);
            }
        });

        toggle = button("KEYS");
        toggle.setContentDescription("Show or hide game keyboard");
        toggle.setElevation(dp(8));
        toggle.setOnClickListener(v -> {
            if (panel.getVisibility() == View.VISIBLE) hide();
            else {
                panel.setVisibility(View.VISIBLE);
                toggle.setText("HIDE");
            }
        });
        RelativeLayout.LayoutParams corner = new RelativeLayout.LayoutParams(dp(72), dp(48));
        corner.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        corner.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        corner.setMargins(dp(8), dp(8), dp(8), 0);
        parent.addView(toggle, corner);
    }

    private int dp(int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    private Button button(String label) {
        Button button = new Button(context);
        button.setText(label);
        button.setTextSize(12);
        button.setTextColor(Color.WHITE);
        button.setBackgroundTintList(ColorStateList.valueOf(0xa035354d));
        button.setPadding(0, 0, 0, 0);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        button.setAllCaps(false);
        // Touch controls must not take D-pad/gamepad or text-editor focus.
        button.setFocusable(false);
        button.setFocusableInTouchMode(false);
        return button;
    }

    private LinearLayout.LayoutParams cell() {
        return new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1);
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(context);
        panel.addView(row, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        return row;
    }

    private LinearLayout letters(String labels) {
        LinearLayout row = row();
        for (char c : labels.toCharArray()) key(row, "" + c, KeyEvent.KEYCODE_A + c - 'A');
        return row;
    }

    private void key(LinearLayout row, String label, int code) {
        Button button = button(label);
        button.setOnClickListener(v -> {
            release(code);
            long downTime = SystemClock.uptimeMillis();
            dispatch(code, KeyEvent.ACTION_DOWN, downTime);
            Runnable up = () -> {
                dispatch(code, KeyEvent.ACTION_UP, downTime);
                releases.remove(code);
            };
            releases.put(code, up);
            // Keep a tap down for several game frames, including polled keys.
            handler.postDelayed(up, 80);
            if (code == KeyEvent.KEYCODE_ESCAPE) {
                panel.setVisibility(View.GONE);
                toggle.setText("KEYS");
            }
        });
        row.addView(button, cell());
    }

    private void dispatch(int code, int action, long downTime) {
        KeyEvent event = new KeyEvent(downTime, SystemClock.uptimeMillis(), action,
                code, 0, 0, KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
        // SDL handles both printable text and ordinary game key events here.
        SDLActivity.handleKeyEvent(panel, code, event, null);
    }

    private void release(int code) {
        Runnable up = releases.get(code);
        if (up != null) {
            handler.removeCallbacks(up);
            up.run();
        }
    }

    void hide() {
        while (releases.size() != 0) release(releases.keyAt(0));
        panel.setVisibility(View.GONE);
        toggle.setText("KEYS");
    }

    /** A controller is in use: close the panel and clear the corner over the pitch. */
    void padUsed() {
        if (toggle.getVisibility() == View.GONE) return;
        hide();
        toggle.setVisibility(View.GONE);
    }

    /** The screen was touched: offer the keys again. */
    void touched() {
        toggle.setVisibility(View.VISIBLE);
    }
}
