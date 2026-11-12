/*
 * match_chat.h -- chatting during an online match (SDL builds; docs/NETPLAY.md, "Chat in a match").
 *
 * The lobby has its own chat on its link (netplay_lobby.c CH_CHAT); the link closes at kick-off,
 * so a match's chat goes through GGPO-X's (ggpo_client_chat: one UDP datagram a line, on the
 * match's own socket, relayed with it). It is never match state: nothing here is saved by a
 * rollback, recorded, or hashed.
 *
 *   TAB          type a line (ENTER sends it, ESC or TAB again drops it). The keyboard does
 *                not play while the line is open.
 *   F3           hide the chat box / show it again
 *   1 .. 0       a quick message ([chat] quick1 .. quick0 in SWOS.ini), when no line is open
 *
 * Lines show in a box at the bottom left of the window, in the chat font (chat_font.h), and fade
 * six seconds after the newest. Each line is also written to SWOS.log as "[chat] me: ..." or
 * "[chat] them: ...", as the lobby's are, so the match report has them.
 */
#ifndef SWOS_MATCH_CHAT_H
#define SWOS_MATCH_CHAT_H

#include <stdint.h>
#include <SDL.h>

#define MATCHCHAT_TEXT_MAX   100        /* a line's bytes of UTF-8 (GGPO-X carries 119) */
#define MATCHCHAT_QUICK      10         /* quick messages, keys 1..9 then 0 */

/* A live match has started / ended: the lines are cleared, the names set ("" = P1/P2). */
void    MatchChat_Begin(const char *myName, const char *theirName);
void    MatchChat_End(void);
int32_t MatchChat_Active(void);         /* a chat is on: a live match (or SWOS_CHAT_DEMO's) */
/* The phones' CHAT button (touch_controls.c): opens a line, with the system's keyboard, or
 * drops the one that is open. */
void    MatchChat_ToggleInput(void);
int32_t MatchChat_Typing(void);

/* THE LINE BEING TYPED, for another screen (the lobby's chat field, online_menus.c): it takes
 * every key and typed character until ENTER (onSend gets the cleaned UTF-8 line, never empty),
 * ESC or TAB. One line at a time: opening it drops the match's, and the other way round. */
void        ChatLine_Open(void (*onSend)(const char *utf8));
void        ChatLine_Close(void);
int32_t     ChatLine_IsOpen(void);
const char *ChatLine_Text(void);

/* An SDL event, before the game sees it: 1 when the chat took it. */
int32_t MatchChat_Event(const SDL_Event *e);
/* A datagram from the other player (NETPLAY_EVENT_CHAT). */
void    MatchChat_Received(const char *message);
/* Each frame: the repeats of what was sent. */
void    MatchChat_Tick(void);
/* Over the picture (platform_sdl.c draw_overlays), in gfx.h's frame. */
void    MatchChat_Draw(void);

/* [chat] quickN (index 0 = key 1, ... 9 = key 0); "" turns that key off. */
void        MatchChat_SetQuick(int32_t i, const char *text);
const char *MatchChat_Quick(int32_t i);
const char *MatchChat_DefaultQuick(int32_t i);

/* What a line may hold: control characters dropped, characters the font lacks as '?', at most
 * MATCHCHAT_TEXT_MAX bytes, cut on a character. Returns the length. */
int32_t MatchChat_Clean(const char *in, char *out, int32_t outSize);

/* SWOS 2020's CHAT WINDOW SCALE ([chat] scale: 0..MATCHCHAT_SCALE_MAX, the box 1x .. 5x in halves;
 * Page Up / Page Down in an online match) and AUTO CLOSE CHATBOX ([chat] auto_close: 1..5, the
 * seconds the box stays after the newest line). For a settings row: read, set (the caller saves). */
#define MATCHCHAT_SCALE_MAX 8
void    MatchChat_SetScale(int32_t scale);
int32_t MatchChat_Scale(void);
void    MatchChat_SetAutoClose(int32_t seconds);
int32_t MatchChat_AutoClose(void);

int32_t MatchChat_SelfTest(void);

/* ---- SWOS 2020's MULTIPLAYER SETTINGS rows (SWOS 2020 port, agent A) ---- */
/* (AUTO CLOSE CHATBOX and CHAT WINDOW SCALE: MatchChat_AutoClose / MatchChat_Scale, above) */
void    MatchChat_Notice(const char *text);         /* a line of the game's own */

#endif
