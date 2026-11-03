/*
 * touch_controls.c -- the phones' on-screen controls (SWOS_MOBILE_BUILD; docs/MOBILE.md): a stick
 * on the left, FIRE on the right, PAUSE and ESC above it, drawn over the picture and played as
 * player 1's -- the web page's (wasm/shell.html), done natively. They report through the same two
 * calls (input_sdl.c InputSDL_TouchControls, InputSDL_TouchKey), so the game sees them as it sees
 * the page's: combined with the keyboard every frame, a press of FIRE a fire event, and sent as
 * this player's in an online match.
 *
 * Multi-touch: the stick and FIRE are held at once, each by its own finger. The stick is 8-way, 45
 * degrees each, nothing inside its dead zone (SWOS's stick is digital). They go when a game
 * controller's button is pressed (it is player 1's then), and come back with the next touch -- not
 * on a controller merely being there: a phone can report one nobody holds (the iOS simulator's).
 *
 * That is AUTO, the default; [input] touch_controls (OPTIONS -> CONTROLS, ON-SCREEN) can say ALWAYS
 * (never hidden) or NEVER (never drawn): a handheld with its own controls, the Retroid / AYN kind,
 * whose built-in pad may not even be a "controller" to SDL (2026-11-03). NEVER still answers a touch
 * where the controls would be -- a phone with nothing else to steer by is not locked out of the menu
 * that would put them back.
 */
#include <SDL.h>
#include <math.h>
#include <string.h>
#include "touch_controls.h"

#ifdef SWOS_MOBILE_BUILD

extern void InputSDL_TouchControls(int32_t dirs, int32_t fire);
extern void InputSDL_TouchKey(int32_t akey);

enum { KEY_P = 0x19, KEY_ESC = 0x45 };          /* (raw Amiga scancodes) */
#define DEADZONE 0.28f                          /* (of the stick's radius) */

static SDL_FingerID s_stickFinger = -1, s_fireFinger[4] = { -1, -1, -1, -1 };
static int   s_dirs, s_fire, s_sent = -1, s_hidden, s_keyDown[2];
static int   s_mode = TOUCH_CONTROLS_AUTO;      /* [input] touch_controls */
static float s_knobX, s_knobY;                  /* (the knob's offset, in output pixels) */

/* The layout, in output pixels, from the output's size (the web page's, in proportion: its
 * 150 px stick and 112 px FIRE on a 360 px tall landscape phone). */
typedef struct { float x, y, r; } Circle;
typedef struct { float x, y, w, h; } Box;
static void layout(int w, int h, Circle *stick, Circle *fire, Box *pause, Box *esc)
{
    const float u = (float)h / 360.0f;
    stick->r = 75 * u; stick->x = 28 * u + stick->r; stick->y = h - 28 * u - stick->r;
    fire->r = 56 * u;  fire->x = w - 32 * u - fire->r; fire->y = h - 44 * u - fire->r;
    esc->w = 52 * u; esc->h = 32 * u; esc->x = w - 32 * u - esc->w; esc->y = h - 172 * u - esc->h;
    *pause = *esc; pause->w = 64 * u; pause->x = esc->x - 10 * u - pause->w;
}

static void send(void)
{
    const int v = s_dirs | (s_fire << 4);
    if (v == s_sent)
        return;
    InputSDL_TouchControls(s_dirs, s_fire);
    s_sent = v;
}

static void set_stick(const Circle *c, float x, float y)
{
    const float dx = x - c->x, dy = y - c->y, d = sqrtf(dx * dx + dy * dy);
    const float k = d > c->r ? c->r / d : 1.0f;
    s_knobX = dx * k;
    s_knobY = dy * k;
    s_dirs = 0;
    if (d > c->r * DEADZONE) {
        int oct = (int)lroundf(atan2f(dy, dx) / ((float)M_PI / 4));    /* -4..4, 0 right */
        oct = (oct + 8) % 8;                                            /* 0 right, 2 down, 4 left, 6 up */
        if (oct == 7 || oct == 0 || oct == 1) s_dirs |= 8;
        if (oct >= 1 && oct <= 3) s_dirs |= 2;
        if (oct >= 3 && oct <= 5) s_dirs |= 4;
        if (oct >= 5 && oct <= 7) s_dirs |= 1;
    }
    send();
}

static int fire_count(void)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += s_fireFinger[i] != -1;
    return n;
}

static int in_box(const Box *b, float x, float y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void release_all(void)
{
    s_stickFinger = -1;
    for (int i = 0; i < 4; i++) s_fireFinger[i] = -1;
    s_dirs = 0; s_fire = 0; s_knobX = s_knobY = 0;
    s_keyDown[0] = s_keyDown[1] = 0;
    send();
}

/* Everything let go: the window lost focus or the app went to the background -- a message box or
 * the system took the finger, and its lift never comes (a FIRE held through it pressed again). */
void TouchControls_SetMode(int mode)
{
    if (mode < TOUCH_CONTROLS_AUTO || mode > TOUCH_CONTROLS_NEVER)
        mode = TOUCH_CONTROLS_AUTO;
    s_mode = mode;
    release_all();
    s_hidden = 0;
}

int TouchControls_Mode(void)
{
    return s_mode;
}

void TouchControls_ReleaseAll(void)
{
    if (s_stickFinger != -1 || s_fire || s_keyDown[0] || s_keyDown[1])
        release_all();
}

/* An event; w, h the renderer's output size (platform_sdl.c). */
void TouchControls_HandleEvent(const SDL_Event *e, int w, int h)
{
    Circle stick, fire;
    Box pause, esc;
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (!s_hidden && s_mode == TOUCH_CONTROLS_AUTO) { s_hidden = 1; release_all(); }
        return;
    }
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP)
        return;
    if (w <= 0 || h <= 0)
        return;
    layout(w, h, &stick, &fire, &pause, &esc);
    const float x = e->tfinger.x * w, y = e->tfinger.y * h;
    const SDL_FingerID id = e->tfinger.fingerId;
    if (e->type == SDL_FINGERDOWN) {
        s_hidden = 0;
        if (in_box(&pause, x, y)) { s_keyDown[0] = 1; InputSDL_TouchKey(KEY_P); return; }
        if (in_box(&esc, x, y))   { s_keyDown[1] = 1; InputSDL_TouchKey(KEY_ESC); return; }
        if (y < h * 0.40f)
            return;                                     /* (the zones: the lower 60 %, as the page's) */
        if (x < w * 0.5f) {
            if (s_stickFinger == -1) { s_stickFinger = id; set_stick(&stick, x, y); }
        } else {
            for (int i = 0; i < 4; i++)
                if (s_fireFinger[i] == -1) { s_fireFinger[i] = id; break; }
            s_fire = 1;
            send();
        }
        return;
    }
    if (e->type == SDL_FINGERMOTION) {
        if (id == s_stickFinger)
            set_stick(&stick, x, y);
        return;
    }
    /* up */
    s_keyDown[0] = s_keyDown[1] = 0;
    if (id == s_stickFinger) {
        s_stickFinger = -1; s_dirs = 0; s_knobX = s_knobY = 0;
        send();
    }
    for (int i = 0; i < 4; i++)
        if (s_fireFinger[i] == id) s_fireFinger[i] = -1;
    if (s_fire && !fire_count()) { s_fire = 0; send(); }
}

/* ---- drawing: in output pixels, over the finished frame ---- */

static void disc(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c)
{
    enum { N = 40 };
    SDL_Vertex v[N + 1];
    int idx[N * 3];
    v[0].position.x = cx; v[0].position.y = cy; v[0].color = c;
    for (int i = 0; i < N; i++) {
        const float a = (float)i * 2.0f * (float)M_PI / N;
        v[i + 1].position.x = cx + rad * cosf(a);
        v[i + 1].position.y = cy + rad * sinf(a);
        v[i + 1].color = c;
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % N + 1;
    }
    SDL_RenderGeometry(r, NULL, v, N + 1, idx, N * 3);
}

static void ring(SDL_Renderer *r, float cx, float cy, float rad, float width, SDL_Color c)
{
    enum { N = 40 };
    SDL_Vertex v[N * 2];
    int idx[N * 6];
    for (int i = 0; i < N; i++) {
        const float a = (float)i * 2.0f * (float)M_PI / N, ca = cosf(a), sa = sinf(a);
        v[i * 2].position.x = cx + rad * ca;             v[i * 2].position.y = cy + rad * sa;
        v[i * 2 + 1].position.x = cx + (rad - width) * ca; v[i * 2 + 1].position.y = cy + (rad - width) * sa;
        v[i * 2].color = v[i * 2 + 1].color = c;
        const int j = (i + 1) % N;
        idx[i * 6] = i * 2; idx[i * 6 + 1] = i * 2 + 1; idx[i * 6 + 2] = j * 2;
        idx[i * 6 + 3] = j * 2; idx[i * 6 + 4] = i * 2 + 1; idx[i * 6 + 5] = j * 2 + 1;
    }
    SDL_RenderGeometry(r, NULL, v, N * 2, idx, N * 6);
}

static void box(SDL_Renderer *r, const Box *b, int down, float u)
{
    SDL_FRect f = { b->x, b->y, b->w, b->h };
    SDL_SetRenderDrawColor(r, 255, 255, 255, down ? 102 : 31);
    SDL_RenderFillRectF(r, &f);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 102);
    for (int i = 0; i < (int)(2 * u + 0.5f); i++) {
        SDL_FRect o = { f.x + i, f.y + i, f.w - 2 * i, f.h - 2 * i };
        SDL_RenderDrawRectF(r, &o);
    }
}

void TouchControls_Draw(SDL_Renderer *r)
{
    int w, h;
    Circle stick, fire;
    Box pause, esc;
    if (s_hidden || s_mode == TOUCH_CONTROLS_NEVER || SDL_GetRendererOutputSize(r, &w, &h) != 0 ||
        w <= 0 || h <= 0)
        return;
    layout(w, h, &stick, &fire, &pause, &esc);
    const float u = (float)h / 360.0f;
    /* (in output pixels: the picture's logical size and scale put aside, then put back) */
    int lw, lh;
    float sx, sy;
    SDL_Rect vp;
    SDL_RenderGetLogicalSize(r, &lw, &lh);
    SDL_RenderGetScale(r, &sx, &sy);
    SDL_RenderGetViewport(r, &vp);
    SDL_RenderSetLogicalSize(r, 0, 0);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_RenderSetViewport(r, NULL);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const SDL_Color faint = { 255, 255, 255, 26 }, edge = { 255, 255, 255, 89 }, knob = { 255, 255, 255, 89 },
                    fireUp = { 255, 255, 255, 36 }, fireDown = { 255, 255, 255, 102 };
    disc(r, stick.x, stick.y, stick.r, faint);
    ring(r, stick.x, stick.y, stick.r, 2 * u, edge);
    disc(r, stick.x + s_knobX, stick.y + s_knobY, 31 * u, knob);
    ring(r, stick.x + s_knobX, stick.y + s_knobY, 31 * u, 2 * u, edge);
    disc(r, fire.x, fire.y, fire.r, s_fire ? fireDown : fireUp);
    ring(r, fire.x, fire.y, fire.r, 2 * u, edge);
    box(r, &pause, s_keyDown[0], u);
    box(r, &esc, s_keyDown[1], u);
    /* their marks: PAUSE two bars, ESC a cross */
    SDL_SetRenderDrawColor(r, 255, 255, 255, 191);
    {
        const float bh = pause.h * 0.5f, bw = 4 * u, cx = pause.x + pause.w / 2, cy = pause.y + pause.h / 2;
        SDL_FRect b1 = { cx - 2 * bw, cy - bh / 2, bw, bh }, b2 = { cx + bw, cy - bh / 2, bw, bh };
        SDL_RenderFillRectF(r, &b1);
        SDL_RenderFillRectF(r, &b2);
    }
    {
        const float s = esc.h * 0.25f, cx = esc.x + esc.w / 2, cy = esc.y + esc.h / 2;
        for (float t = -1.5f * u; t <= 1.5f * u; t += 1.0f) {
            SDL_RenderDrawLineF(r, cx - s + t, cy - s, cx + s + t, cy + s);
            SDL_RenderDrawLineF(r, cx - s + t, cy + s, cx + s + t, cy - s);
        }
    }
    {   /* FIRE's: a ball's ring */
        ring(r, fire.x, fire.y, fire.r * 0.32f, 3 * u, (SDL_Color){ 255, 255, 255, 170 });
    }

    SDL_RenderSetLogicalSize(r, lw, lh);
    if (!lw)
        SDL_RenderSetScale(r, sx, sy);
    SDL_RenderSetViewport(r, lw ? NULL : &vp);
}

#else   /* the desktops have no on-screen controls: the setting is kept, and does nothing */

static int s_mode;
void TouchControls_SetMode(int mode) { s_mode = mode; }
int  TouchControls_Mode(void)        { return s_mode; }

#endif /* SWOS_MOBILE_BUILD */
