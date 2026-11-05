/*
 * online_account.c -- OpenSWOS's accounts: none (include/online_account.h).
 *
 * A fork with accounts sets PROJECT_ACCOUNTS (include/project_identity.h) and supplies these
 * functions from a file of its own; this one then compiles to nothing.
 */
#include "online_account.h"
#include "project_identity.h"

#ifndef PROJECT_ACCOUNTS
#define PROJECT_ACCOUNTS 0
#endif

#if !PROJECT_ACCOUNTS

/* A mod's provider (ModApi_SetOnlineProvider, docs/MODS.md §25): each function asks it first,
 * else OpenSWOS's answer -- no accounts. */
#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#include "mod_api.h"
static ModOnlineProvider s_provider;
static int32_t s_hasProvider;

int32_t ModApi_SetOnlineProvider(const char *slug, const ModOnlineProvider *provider)
{
    if (!provider)
        return 1;
    s_provider = *provider;
    s_hasProvider = 1;
    fprintf(stderr, "[online] %s is the online provider (%s, matchmaker %s)\n", slug ? slug : "?",
            provider->name ? provider->name : "?", provider->mmHost ? provider->mmHost : "the default");
    return 0;
}

const char *OnlineProvider_MmHost(void) { return s_hasProvider ? s_provider.mmHost : 0; }
uint16_t    OnlineProvider_MmPort(void) { return s_hasProvider ? s_provider.mmPort : 0; }
#define P(fn) (s_hasProvider && s_provider.fn)

void        Account_Init(void)                 { if (P(init)) s_provider.init(); }
int32_t     Account_Has(AccountFeature feature) { return P(has) ? s_provider.has(feature) : 0; }
int32_t     Account_Enabled(void)              { return P(enabled) ? s_provider.enabled() : 0; }
const char *Account_Prompt(void)
{
    return P(prompt) ? s_provider.prompt() : "CHOOSE A NAME AND THE SERVER TO MEET ON";
}
const char *Account_Url(AccountLink link)      { return P(url) ? s_provider.url(link) : 0; }
void        Account_Begin(const char *user, const char *password)
{
    if (P(begin))
        s_provider.begin(user, password);
}
int32_t     Account_Poll(void)                 { return P(poll) ? s_provider.poll() : 1; }
const char *Account_Error(void)                { return P(error) ? s_provider.error() : ""; }
int32_t     Account_ChooseMode(void)           { return P(chooseMode) ? s_provider.chooseMode() : 0; }
void        Account_LobbyOpened(int32_t host)  { if (P(lobbyOpened)) s_provider.lobbyOpened(host); }
void        Account_LobbyPeer(const char *name) { if (P(lobbyPeer)) s_provider.lobbyPeer(name); }
void        Account_LobbyPoll(void)            { if (P(lobbyPoll)) s_provider.lobbyPoll(); }
int32_t     Account_LobbyCanStart(void)        { return P(lobbyCanStart) ? s_provider.lobbyCanStart() : 1; }
const char *Account_MatchText(void)            { return P(matchText) ? s_provider.matchText() : 0; }
void        Account_SelectMatch(void)          { if (P(selectMatch)) s_provider.selectMatch(); }
void        Account_UserSelected(const char *name) { if (P(userSelected)) s_provider.userSelected(name); }
void        Account_MatchPlayed(const AccountResult *result)
{
    if (P(matchPlayed))
        s_provider.matchPlayed(result);
}
#undef P

#else   /* the Amiga: no mods with code, OpenSWOS's own answers */

void        Account_Init(void)                 {}
int32_t     Account_Has(AccountFeature feature) { (void)feature; return 0; }
int32_t     Account_Enabled(void)              { return 0; }
const char *Account_Prompt(void)               { return "CHOOSE A NAME AND THE SERVER TO MEET ON"; }
const char *Account_Url(AccountLink link)      { (void)link; return 0; }
void        Account_Begin(const char *user, const char *password) { (void)user; (void)password; }
int32_t     Account_Poll(void)                 { return 1; }
const char *Account_Error(void)                { return ""; }
int32_t     Account_ChooseMode(void)           { return 0; }
void        Account_LobbyOpened(int32_t host)  { (void)host; }
void        Account_LobbyPeer(const char *name) { (void)name; }
void        Account_LobbyPoll(void)            {}
int32_t     Account_LobbyCanStart(void)        { return 1; }
const char *Account_MatchText(void)            { return 0; }
void        Account_SelectMatch(void)          {}
void        Account_UserSelected(const char *name) { (void)name; }
void        Account_MatchPlayed(const AccountResult *result) { (void)result; }

#endif  /* SWOS_SDL_BUILD */

#endif
