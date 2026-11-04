#ifndef SWOS_AI_IDENTITY_JOURNAL_H
#define SWOS_AI_IDENTITY_JOURNAL_H
/* Append-only journal of canonical AI exchanges stored in CAR v4.
 * NOT an authoritative player identity registry. */
void SWOS_AI_IdentityJournalClear(void);
unsigned SWOS_AI_IdentityJournalCount(void);
unsigned SWOS_AI_IdentityJournalDropped(void);
int SWOS_AI_IdentityJournalGet(unsigned index, unsigned values[6]);
int SWOS_AI_IdentityJournalAdd(unsigned file_a, unsigned team_a, unsigned slot_a,
                               unsigned file_b, unsigned team_b, unsigned slot_b);
#endif
