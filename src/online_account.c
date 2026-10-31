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

void        Account_Init(void)                 {}
int32_t     Account_Has(AccountFeature feature) { (void)feature; return 0; }
int32_t     Account_Enabled(void)              { return 0; }
const char *Account_Prompt(void)               { return "CHOOSE A NAME AND THE SERVER TO MEET ON"; }
const char *Account_Url(AccountLink link)      { (void)link; return 0; }
void        Account_Begin(const char *user, const char *password) { (void)user; (void)password; }
int32_t     Account_Poll(void)                 { return 1; }
const char *Account_Error(void)                { return ""; }

#endif
