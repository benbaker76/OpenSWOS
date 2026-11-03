/*
 * srp_resim.c -- play a recorded match again, exactly: --srp-resim=<file.SRP>.
 *
 * A resumable .SRP (include/replay_v2.h, docs/REPLAY_FORMAT_V2.md) holds what the match
 * was set up from, the state it kicked off in, and every frame's input and SIM hash. This
 * sets the match up from them, plays it again headless through the netplay step (the
 * same step a live match takes, netplay_match.c's resim mode), and checks every frame
 * against the recording.
 *
 * On the build and the rules it was recorded with, every frame must agree. The first
 * that does not is where THIS machine plays the match differently from the one that
 * recorded it: a cross-platform desync, reproduced offline, from a match that was played
 * once, anywhere -- the recorder keeps the same file for every match it is streamed.
 *
 *   bin/SWOS.x86_64 --srp-resim=recordings/20260919-002137-2e219cdf.SRP
 *   -> [resim] ... 2000 frames, every one as recorded          exit 0
 *   -> [resim] ... parts at frame 812 (here 1a2b3c4d, then ...)  exit 2
 *
 * SWOS_SRP_DIFF_KICKOFF=<other.SRP> compares the two recordings' kick-off states instead and
 * names every match field they disagree on -- the other player's own copy of a match that
 * went out of step at frame 0 (a LATEST.SRP from the joiner, beside the recorder's copy),
 * the joiner having built a different match from the same setup.
 *
 * Refused (exit 1) when the recording's registry layout or build rules are not this
 * build's: such a recording is not the same match here, and a difference would prove
 * nothing. The gameplay style and the fire-hold lockout are taken from the recording.
 */
#include "srp_resim.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_NETPLAY

#include "game_types.h"
#include "image_cmd.h"          /* ProcessImageCommandStream */
#include "match_sim.h"          /* InitTeams */
#include "netplay_match.h"
#include "netplay_setup.h"
#include "test_rules.h"
#include "p2p_link.h"           /* P2PLink_GameplayFlags, P2P_RULE_* */
#include "replay_v2.h"
#include "sim_state.h"
#include "state_wire.h"         /* StateWire_Peek: SWOS_SRP_DIFF_KICKOFF */
#include "miniz_swos.h"         /* tinfl_decompress_mem_to_mem */
#include "game_loop.h"          /* _gFrameStepInput */
#include "dos_mode.h"           /* _gDosView: the view it was replayed with */

extern TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];
extern uint16_t   _numSelectedTeams;
extern uint16_t   _menuBGImageCmdStream[];
extern uint8_t   *_menuImageStreamBaseEnd;
extern void       InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2);

static const char   *s_path;
static ReplayResume  s_rec;
static int32_t       s_loaded;

int32_t SrpResim_Arm(const char *path)
{
    s_path = path;
    return path && path[0] ? 0 : 1;
}

int32_t SrpResim_Armed(void)
{
    return s_path != NULL;
}

/* Read it now -- at the command line, before the options are published -- so the match is
 * played by the rules it was recorded with. */
int32_t SrpResim_Rules(int *dosGameplay, int *fireHoldLockout, int *dosView)
{
    int32_t rc;
    if (!s_loaded) {
        rc = ReplayV2_LoadResume(s_path, &s_rec);
        if (rc != 0) {
            fprintf(stderr, "[resim] %s: %s\n", s_path,
                    rc == 1 ? "not a .SRP replay" :
                    rc == 2 ? "recorded to be watched only -- no setup, kick-off state or inputs" :
                              "its resume parts do not add up");
            return 1;
        }
        s_loaded = 1;
    }
    /* The style (GAMEPLAY_*): SIMV's own byte, or, before XBOX, the rules byte's DOS bit. */
    *dosGameplay = s_rec.style ? s_rec.style : (s_rec.rules & P2P_RULE_DOS) ? GAMEPLAY_DOS : GAMEPLAY_AMIGA;
    *fireHoldLockout = (s_rec.rules & P2P_RULE_FIRE_LOCKOUT) != 0;
    /* The view is a rule too (P2PLink_GameplayFlags). A recording from before it said
     * so plays by this machine's own -- see the hint if it parts at frame 1. */
    *dosView = (s_rec.rules & P2P_RULE_VIEW_KNOWN) ? (s_rec.rules & P2P_RULE_DOS_VIEW) != 0 : -1;
    return 0;
}

/* The match is over -- the recording ran out, a frame parted from it, or the match left
 * its loop. Say which, and exit from inside the match (NetplayMatch_SetFinishedCallback). */
static void resim_finished(void)
{
    uint32_t played = 0, matched = 0, have = 0, want = 0;
    int32_t firstDiff = -1;

    NetplayMatch_ResimResult(&played, &matched, &firstDiff, &have, &want);
    if (firstDiff < 0 && matched == s_rec.frames) {
        uint32_t vf[2], vs[2], vk[2];
        printf("[resim] %u frames, every one as recorded\n", (unsigned)matched);
        NetplayMatch_ResimViewStats(vf, vs, vk);
        if (vf[0] || vf[1] || vk[0] || vk[1])
            printf("[resim] views: player 1's game replayed for %u guessed frames, %u put everything "
                   "where the recording says his screen drew it (%u not replayable); player 2's %u, %u "
                   "(%u)\n", (unsigned)vf[0], (unsigned)vs[0], (unsigned)vk[0], (unsigned)vf[1],
                   (unsigned)vs[1], (unsigned)vk[1]);
        fflush(stdout);
        exit(0);
    }
    if (firstDiff >= 0)
        printf("[resim] parts at frame %d: this build %08X, the recording %08X -- frames 0..%d "
               "agreed%s\n", (int)firstDiff, (unsigned)have, (unsigned)want, (int)firstDiff - 1,
               firstDiff == 0 ? " (none: the kick-off state itself differs)" : "");
    if (firstDiff == 1 && !(s_rec.rules & P2P_RULE_VIEW_KNOWN))
        printf("[resim] this recording does not say which view it was played on, and the view "
               "moves the camera: try it with --view=%s\n", _gDosView ? "amiga" : "dos");
    else
        printf("[resim] the match ended after %u of %u frames\n", (unsigned)played,
               (unsigned)s_rec.frames);
    fflush(stdout);
    exit(2);
}

/* A kick-off state (STAT) inflated to its portable buffer; NULL if it is not one. */
static uint8_t *kickoff_buffer(const uint8_t *packet, uint32_t len, uint32_t *size)
{
    StateWireInfo h;
    uint8_t *buf;
    if (StateWire_Peek(packet, len, &h) != STATE_WIRE_OK || (h.flags & STATE_WIRE_DELTA) ||
        h.layout != SimState_LayoutHash() || h.rawBytes != SimState_PortableSize(h.tiers))
        return NULL;
    buf = (uint8_t *)malloc(h.rawBytes ? h.rawBytes : 1);
    if (buf && tinfl_decompress_mem_to_mem(buf, h.rawBytes, packet + STATE_WIRE_HEADER_BYTES,
                                           h.packedBytes, 0) != h.rawBytes) {
        free(buf);
        buf = NULL;
    }
    *size = h.rawBytes;
    return buf;
}

/* SWOS_SRP_DIFF_KICKOFF: the match fields two recordings' kick-off states disagree on. The
 * SIM tier's LOCAL fields (one display's, zero in the hash the peers compare) are left out. */
static void diff_kickoff(const char *otherPath)
{
    ReplayResume other;
    uint8_t *a, *b, *mask;
    uint32_t sa = 0, sb = 0, offset = 0, shown = 0, fields = 0;
    int32_t i;

    if (ReplayV2_LoadResume(otherPath, &other) != 0) {
        printf("[resim] %s: no kick-off state to compare\n", otherPath);
        exit(1);
    }
    a = kickoff_buffer(s_rec.kickoff, s_rec.kickoffLen, &sa);
    b = kickoff_buffer(other.kickoff, other.kickoffLen, &sb);
    if (!a || !b || sa != sb) {
        printf("[resim] the two kick-off states cannot be compared (another build's, or not states)\n");
        exit(1);
    }
    printf("[resim] kick-off states: %s against %s\n", s_path, otherPath);
    for (i = 0; i < g_simStateRegionCount; i++) {
        const uint32_t size = SimState_RegionActive(i, SIM_TIER_ALL) ? SimState_RegionPortableSize(i) : 0;
        const int32_t sim = SimState_RegionActive(i, SIM_TIER_SIM);
        uint32_t k;
        if (!size)
            continue;
        mask = (uint8_t *)calloc(size, 1);
        if (mask && sim)
            SimState_PortableLocalMask(i, mask);
        for (k = 0; k < size; k++) {
            char where[96];
            if (a[offset + k] == b[offset + k] || (mask && mask[k]))
                continue;
            fields++;
            if (shown++ < 200) {
                SimState_DescribePortableByte(i, k, where, sizeof where);
                printf("  %-4s %-36s %-26s %02X vs %02X\n", sim ? "SIM" : "view",
                       g_simStateRegions[i].name, where, a[offset + k], b[offset + k]);
            }
        }
        free(mask);
        offset += size;
    }
    printf("[resim] %u differing byte%s\n", (unsigned)fields, fields == 1 ? "" : "s");
    fflush(stdout);
    exit(fields ? 2 : 0);
}

void SrpResim_Run(void)
{
    uint8_t here = P2PLink_GameplayFlags();

    if (getenv("SWOS_SRP_DIFF_KICKOFF"))
        diff_kickoff(getenv("SWOS_SRP_DIFF_KICKOFF"));

    printf("[resim] %s: %u frames, recorded by build layout %016llx, rules 0x%02X, sim width %u\n",
           s_path, (unsigned)s_rec.frames, (unsigned long long)s_rec.layout, (unsigned)s_rec.rules,
           (unsigned)s_rec.simWidth);
    if (s_rec.layout != SimState_LayoutHash()) {
        printf("[resim] REFUSED: recorded by a build whose match state is laid out differently "
               "(this build %016llx)\n", (unsigned long long)SimState_LayoutHash());
        exit(1);
    }
    if (s_rec.rules & P2P_RULE_ARCADE) {
        printf("[resim] REFUSED: recorded by a build that plays by other rules (0x%02X here)\n",
               (unsigned)here);
        exit(1);
    }
    /* The rule the match was PLAYED under. A tester build before 2026-10-11 was compiled
     * without cards and injuries and said so in two bits; that is the testers' rule
     * (include/test_rules.h), put in force here for the replay. One bit without the
     * other was a mixed build -- the binary that desynced a live match -- and cannot be
     * replayed by anything. */
    {
        uint8_t rule = (uint8_t)(s_rec.rules & P2P_RULES_BUILD);
        if (rule != 0 && rule != P2P_RULES_BUILD) {
            printf("[resim] REFUSED: recorded by a mixed build (rules 0x%02X: cards and "
                   "injuries disagree)\n", (unsigned)s_rec.rules);
            exit(1);
        }
#ifndef SWOS_TEST_RULES
        if (rule) {
            printf("[resim] REFUSED: recorded without cards and injuries, and this build "
                   "has that rule compiled out (make SWOS_TEST_RULES=1)\n");
            exit(1);
        }
#endif
        TestRules_Force(rule ? 1 : 0);
        if (rule)
            printf("[resim] played without cards and injuries (an old tester build, or the host's "
                   "CARDS/INJURIES switch): replayed by that rule\n");
    }
    if (NetplaySetup_Apply(s_rec.setup, s_rec.setupLen) != 0) {
        printf("[resim] REFUSED: the recording's match setup does not apply here\n");
        exit(1);
    }
    /* As the live test and the replay rig do: the front end was skipped, so the menu
     * sprites the stadium line-up draws were never extracted. */
    ProcessImageCommandStream(_menuBGImageCmdStream, _menuImageStreamBaseEnd);
    InitTeams();
    ReplayRec_Suppress(1);          /* a re-simulation is not a match to keep */
    /* Played before the netplay step owned the clocks: two ticks a frame, as then. */
    if (!(s_rec.rules & P2P_RULE_ONE_TICK)) {
        NetplayMatch_SetLegacyDoubleClock(1);
        printf("[resim] recorded with the old double clock (two ticks a frame); replayed the same way\n");
    }
    NetplaySetup_ArmTieRules(1);    /* a cup tie is settled as it was live */

    /* With --contest-log: what each player's own game showed, too. */
    {
        uint8_t *pred = NULL, *seen = NULL;
        uint32_t seenLen = 0;
        if (ReplayV2_LoadPrediction(s_path, s_rec.frames, &pred, &seen, &seenLen) == 0)
            NetplayMatch_SetResimViews(pred, seen, seenLen);
    }
    if (NetplayMatch_StartResim(s_rec.kickoff, s_rec.kickoffLen, s_rec.inputs, s_rec.hashes,
                                s_rec.frames, s_rec.simWidth) != 0) {
        printf("[resim] the match would not start\n");
        exit(1);
    }
    NetplayMatch_SetFinishedCallback(resim_finished);
    _gFrameStepInput = 1;
    if (_numSelectedTeams < 2)
        _numSelectedTeams = 2;
    InitAndPlayGame(&_selectedTeams[0], &_selectedTeams[1]);
    /* Only if the match never reached its loop. */
    printf("[resim] the match never started\n");
    fflush(stdout);
    exit(2);
}

#else  /* !SWOS_NETPLAY */

int32_t SrpResim_Arm(const char *path) { (void)path; return 1; }
int32_t SrpResim_Armed(void) { return 0; }
int32_t SrpResim_Rules(int *dosGameplay, int *fireHoldLockout, int *dosView)
{
    (void)dosGameplay; (void)fireHoldLockout; (void)dosView;
    return 1;
}
void    SrpResim_Run(void) {}

#endif /* SWOS_NETPLAY */
#endif /* SWOS_SDL_BUILD */
