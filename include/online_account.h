/*
 * online_account.h -- accounts on a project's online service (SDL builds with netplay).
 *
 * OpenSWOS has none: a player types a name and plays, and the matchmaking server never asks
 * who they are (src/online_account.c). A fork with accounts -- SWOS United, whose players log
 * in to sensiblesoccer.de -- sets PROJECT_ACCOUNTS in include/project_identity.h and supplies
 * these functions from a file of its own; OpenSWOS's file compiles to nothing then. Everything
 * the multiplayer screens need to know about accounts goes through here (src/online_menus.c):
 * whether a password is asked for and checked, the web pages to send a player to, and the
 * check itself.
 *
 * Whatever a fork's accounts are, the CHAT is not one of their services: it runs over the
 * lobby's own link between the two players (netplay_lobby.h).
 */
#ifndef ONLINE_ACCOUNT_H
#define ONLINE_ACCOUNT_H

#include "swos_types.h"

typedef enum {
    ACCOUNT_LINK_REGISTER,          /* REGISTER */
    ACCOUNT_LINK_PASSWORD,          /* MY PASSWORD? */
    ACCOUNT_LINK_USERNAME,          /* MY USERNAME? */
    ACCOUNT_LINK_LEAGUES,           /* the online leagues (the LEAGUE / CUP screen) */
    ACCOUNT_LINK_DISCORD,           /* where to apply for one (the same screen) */
    ACCOUNT_LINKS
} AccountLink;

/* A password is asked for and checked at LOGIN. 0: a name is all. */
int32_t     Account_Enabled(void);
/* The log-in screen's line under its title ("LOGIN TO YOUR SENSIBLESOCCER.DE ACCOUNT"). */
const char *Account_Prompt(void);
/* A web page, NULL for none (its button is hidden). */
const char *Account_Url(AccountLink link);

/* The check, off the menu's thread: Begin, then Poll each frame until it is not 0 --
 * 1 the account is good, -1 it is not (Account_Error says why, in the menus' capitals). */
void        Account_Begin(const char *user, const char *password);
int32_t     Account_Poll(void);
const char *Account_Error(void);

#endif /* ONLINE_ACCOUNT_H */
