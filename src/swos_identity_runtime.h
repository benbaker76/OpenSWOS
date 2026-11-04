#ifndef SWOS_IDENTITY_RUNTIME_H
#define SWOS_IDENTITY_RUNTIME_H
#define SWOS_IDENTITY_EXPECTED_COUNT 23264u
/* AI-only shadow registry. Does not yet cover ordinary transfers or generated players. */
unsigned SWOS_IdentitySeed(void);
void SWOS_IdentityClear(void);
void SWOS_IdentityAdvanceSeason(void);
unsigned SWOS_IdentityCount(void);
unsigned SWOS_IdentityGet(unsigned index, unsigned values[4]);
unsigned SWOS_IdentitySwap(unsigned file_a,unsigned team_a,unsigned slot_a,
                           unsigned file_b,unsigned team_b,unsigned slot_b);
unsigned SWOS_IdentityRestore(const unsigned *records,unsigned count,unsigned next_id);
unsigned SWOS_IdentityNextId(void);
unsigned SWOS_IdentityCheck(void);
/* Non-mutating cross-registry audit. Canonical/manual mirrors are expected. */
unsigned SWOS_IdentityAudit(void);
/* Experimental, checked club-to-canonical-slot lookup. Returns zero on an unmapped club. */
void SWOS_IdentityRegisterClub(unsigned club,unsigned file,unsigned record);
void SWOS_IdentityInvalidateClub(unsigned club);
unsigned SWOS_IdentityAgeForClub(unsigned club,unsigned slot,unsigned *age,unsigned *id);
/* Supplementary records for newly generated footballers. Original seed slots remain unchanged. */
unsigned SWOS_GeneratedIdentityCreate(unsigned team,unsigned slot,unsigned age);
unsigned SWOS_GeneratedIdentityCount(void);
unsigned SWOS_GeneratedIdentityGet(unsigned index,unsigned values[4]);
unsigned SWOS_GeneratedIdentityRestore(const unsigned *records,unsigned count,unsigned next_id);
/* Move an ID when an actual 38-byte player record is copied to a new slot.
 * A destination ID is retired when the destination is overwritten. */
unsigned SWOS_GeneratedIdentityMove(unsigned from_team,unsigned from_slot,unsigned to_team,unsigned to_slot);
unsigned SWOS_GeneratedIdentityRetire(unsigned team,unsigned slot);
/* Step 3D manual original-player roster-copy records, including tombstones. */
unsigned SWOS_ManualIdentityCopy(unsigned srcClub,unsigned srcSlot,unsigned dstClub,unsigned dstSlot);
/* Assign a fresh age-17 identity to an AI replacement after a human purchase. */
unsigned SWOS_ManualIdentityCreateYouth(unsigned club,unsigned slot);
unsigned SWOS_ManualIdentityAISwap(unsigned clubA,unsigned slotA,unsigned clubB,unsigned slotB);
unsigned SWOS_ManualIdentityAge(unsigned club,unsigned slot,unsigned *age,unsigned *id);
unsigned SWOS_ManualIdentityIsTouched(unsigned club,unsigned slot);
unsigned SWOS_ManualIdentityPrepareSave(void);
unsigned SWOS_ManualIdentityCount(void);
unsigned SWOS_ManualIdentityGet(unsigned index,unsigned values[5]);
unsigned SWOS_ManualIdentityRestore(const unsigned *rows,unsigned count);
/* Step 4F: persistent retired-ID ledger, optional CAR v4 extension. */
unsigned SWOS_RetiredIdentityCount(void);
unsigned SWOS_RetiredIdentityGet(unsigned index,unsigned values[5]);
unsigned SWOS_RetiredIdentityContains(unsigned id);
unsigned SWOS_RetiredIdentityRestore(const unsigned *rows,unsigned count);
#endif
