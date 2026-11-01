/*
 * netplay_setup.h -- the match setup one peer sends the other before kick-off.
 *
 * The teams, the user tactics, the match options and the random seed: the thing
 * a kick-off state is DERIVED from, not the state itself. Both peers then run
 * the same deterministic init over it and agree on every frame -- the same way
 * the corpus two-peer test gets two identical machines from one recording.
 *
 * ~3.6 KB. p2p_link chunks it across (include/p2p_link.h, "the match setup").
 */
#ifndef NETPLAY_SETUP_H
#define NETPLAY_SETUP_H

#ifdef SWOS_SDL_BUILD

#include "swos_types.h"
#include "game_types.h"

/* One team record as it crosses the wire: the 684 bytes of TeamFileEx, with its one
 * multi-byte field (globalTeamNumber) big-endian. The lobby sends each player's team
 * this way too (src/netplay_lobby.c). */
#define NETPLAY_TEAM_BYTES 684
void     NetplaySetup_WriteTeam(uint8_t *out, const TeamFileEx *team);
void     NetplaySetup_ReadTeam(TeamFileEx *team, const uint8_t *in);

/* Bytes a setup can take; the buffer to hand NetplaySetup_Encode. */
uint32_t NetplaySetup_MaxBytes(void);

/* Pack this machine's current match setup. Returns the size, or -1. */
int32_t  NetplaySetup_Encode(uint8_t *out, uint32_t cap);

/* Adopt a peer's. 0 on success; non-zero leaves this machine's setup alone. */
int32_t  NetplaySetup_Apply(const uint8_t *in, uint32_t len);

/* The setup carries how a draw is settled -- extra time, penalties, a second leg and
 * the first leg's score (the lobby's cup rules). The friendly init clears those, so
 * the setup's are put back from inside it: Arm(1) just before InitAndPlayGame and the
 * init Takes them, once (Arm(0) if the match then does not start). Unarmed, Take does
 * nothing, so a friendly from the menus is never a cup tie left over from online. */
void     NetplaySetup_ArmTieRules(int32_t armed);
void     NetplaySetup_TakeTieRules(void);
/* The host's crowd for pitch file `mapIndex` (include/pitch.h), once, for the match the
 * setup was armed for (NetplaySetup_ArmTieRules); NULL when there is none. */
const uint16_t *NetplaySetup_TakeCrowd(uint16_t mapIndex);

/* Round-trip check: encode, scribble over every global it covers, apply, and
 * require equality. Part of --netplay-selftest. 0 on success. */
int32_t  NetplaySetup_SelfTest(void);

#endif /* SWOS_SDL_BUILD */
#endif /* NETPLAY_SETUP_H */
