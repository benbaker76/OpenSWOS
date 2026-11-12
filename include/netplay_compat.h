/*
 * netplay_compat.h -- which builds can play each other online.
 *
 * Two games play a match only when NETPLAY_COMPAT is the same (src/p2p_link.c), and
 * the matchmaking server lists a game only to players with the same number
 * (VERSION:OPENSWOS/<n>, mm_client.c; Connections.cpp). Until 2026-10-25 the two had
 * to be the same COMMIT, which turned away most players who had not updated that
 * day, for changes that could never have parted their matches.
 *
 * RAISE IT whenever a change could make two games play a match differently:
 *   - anything the match simulation does (the SIM tier, src/sim_state.c), its
 *     input, clock or random numbers -- the replay and netplay rigs show it;
 *   - the rules a match is played by (team data, tactics, a host switch);
 *   - what the two games say to each other: the link (p2p_link.c), the lobby
 *     (netplay_lobby.c), GGPO-X, or the recorder's stream.
 *
 * scripts/check_netplay_compat.sh catches the first kind: it plays the test
 * recordings and FAILS when the SIM hashes change and this number did not
 * (record: tests/netplay_compat.txt, --update after raising it). It cannot see the
 * other two, nor a match the recordings never reach (a booking, a penalty
 * shoot-out) -- those are on whoever makes the change. A build with uncommitted
 * changes plays only its own commit (the handshake's dirty flag): its number
 * vouches for nothing.
 *
 * A desync between two different commits with the same number means a raise was
 * missed: the recorder names both builds (docs/NETPLAY.md §7).
 */
#ifndef NETPLAY_COMPAT_H
#define NETPLAY_COMPAT_H

#define NETPLAY_COMPAT 14     /* 14: main's 13 and mod41-match's 13 merged (both changes in one build); 13 (main): SWOS 2020's AMIGA60 and MOON gameplay styles (the handshake's style byte 3 and 4: dos_mode.h); 13 (mod41-match): a world's stoppage time -- the host's switch 0x20 and its state in the match state (stoppage.c); 12: a two-legged tie's first leg leaves its injuries and sendings-off on the second (netplay_lobby.c leg_carry); 11: the replay ring is 38000 bytes on SDL, the resync state with it (10: the handshake carries the mods that change the match; 9: the XBOX gameplay style, the handshake's style byte; 8: the host's crowd in the setup; 7: the lobby's match mode and service ids; 6: the lobby chat) */

#endif
