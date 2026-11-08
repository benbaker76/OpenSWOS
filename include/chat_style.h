/*
 * chat_style.h -- which font the chat is drawn in: one switch, so a build without the overlay
 * (the Amiga, should it ever play online) keeps a chat.
 *
 *   SWOS_CHAT_DYNAMIC_FONT 1   the chat font (chat_font.h): Roboto Condensed rasterized at the
 *                              window's own pixels and drawn OVER the picture (gfx.h) -- small
 *                              letters, accents, lines of up to 100 bytes of UTF-8 (match_chat.h).
 *                              The lobby's chat (online_menus.c OnlineMenus_DrawOverlay) and the
 *                              in-match chat box (match_chat.c).
 *   SWOS_CHAT_DYNAMIC_FONT 0   the game's own font, drawn INTO the picture as every menu's text
 *                              is: the lobby's chat rows and field as SWOS 2020 had them -- 28
 *                              capitals a line, typed through the game's InputText. There is no
 *                              in-match chat box this way: it would have to be drawn into the
 *                              match's picture (not done).
 *
 * Default: 1 on the SDL builds, 0 elsewhere. `make SWOS_CHAT_DYNAMIC_FONT=0` builds the fixed
 * font on a PC, to try it there. The two talk to each other: an older or fixed-font game takes
 * the longer lines as capitals, '?' for what it cannot draw, cut at 28 (netplay_lobby.c).
 */
#ifndef SWOS_CHAT_STYLE_H
#define SWOS_CHAT_STYLE_H

#ifndef SWOS_CHAT_DYNAMIC_FONT
#ifdef SWOS_SDL_BUILD
#define SWOS_CHAT_DYNAMIC_FONT 1
#else
#define SWOS_CHAT_DYNAMIC_FONT 0
#endif
#endif

#endif
