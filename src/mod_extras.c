/*
 * mod_extras.c -- the engine's side of SWOS 2020's match extras (include/mod_api.h, "match extras";
 * docs/MODS.md §35), for the mods that port them: AUTOPLAY's team control, the ball for its trails,
 * the HUD's sprites for the new scoreboard, the next match's pitch and PLAY MATCH for SELECT PITCH,
 * and the connection for the network quality overlay. SDL builds (the Amiga runs none of those
 * mods: their drawing is matchDrawn's, their keys SDL's).
 *
 * What changes how a match plays (a team's control, the pitch) only happens in an offline match
 * that is not a rig's (ModApi_OfflineMatch), as the offline tweaks do (tweaks.c Tweaks_On).
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD
#include "game_types.h"
#include "netplay_match.h"
#include "replay_test.h"    /* ReplayTest_Running */
#include "srp_resim.h"      /* SrpResim_Armed */
#include "rollback_rig.h"   /* Rollback_Enabled */

#include <stdio.h>
#include <string.h>

extern int32_t CareerAutopilot_Active(void);
extern int32_t ModMatch_Active(void);
extern int32_t NetplayMatch_Resimulating(void);  /* netplay_match.c */
extern Sprite _ballSprite, _currentTimeSprite, _currentPlayerNameSprite;
extern TeamGeneralInfo _topTeamData, _bottomTeamData;

static int32_t rigs_or_online(void)
{
    return NetplayMatch_Enabled() || ReplayTest_Running() || SrpResim_Armed() || Rollback_Enabled() ||
           CareerAutopilot_Active();
}

int32_t ModApi_OfflineMatch(void) { return ModApi_InMatch() && !rigs_or_online(); }
int32_t ModApi_AnyModMatch(void) { return ModMatch_Active(); }

/* ---- the ball ---- */
int32_t ModApi_Ball(ModBall *out)
{
    if (!out || !ModApi_InMatch())
        return 0;
    out->x = _ballSprite.x.s.i;
    out->y = _ballSprite.y.s.i;
    out->z = _ballSprite.z.s.i;
    out->deltaX = _ballSprite.deltaX.s.i;
    out->deltaY = _ballSprite.deltaY.s.i;
    out->deltaZ = _ballSprite.deltaZ.s.i;
    out->speed = _ballSprite.speed;
    out->free = (uint8_t)(_topTeamData.playerHasBall == 0 && _bottomTeamData.playerHasBall == 0);
    return 1;
}

int32_t ModApi_SpriteKind(const struct Sprite *sprite)
{
    if (sprite == &_ballSprite)
        return MODSPRITE_BALL;
    if (sprite == &_currentTimeSprite)
        return MODSPRITE_CLOCK;
    if (sprite == &_currentPlayerNameSprite)
        return MODSPRITE_PLAYER_NAME;
    return MODSPRITE_OTHER;
}

/* ---- a team's control (SWOS 2020's setAutoPlayMode / closeAutoPlayMode) ----
 * The computer plays a team while its playerNumber is 0 (as SWOS 2020 sets it each frame);
 * the number it had is kept here and given back. */
static struct { uint8_t taken; int16_t playerNumber; } s_control[2];

static TeamGeneralInfo *team_info(int32_t team)
{
    return _topTeamData.teamNumber == team + 1 ? &_topTeamData : &_bottomTeamData;
}

/* Every team its own again: at the final whistle and the next kick-off (mod_match.c). */
void ModExtras_RestoreControl(void)
{
    for (int32_t t = 0; t < 2; t++)
        if (s_control[t].taken) {
            if (ModApi_InMatch())
                team_info(t)->playerNumber = s_control[t].playerNumber;
            s_control[t].taken = 0;
        }
}

/* A new match: nothing taken (its teams were just set up afresh). */
void ModExtras_KickOff(void)
{
    s_control[0].taken = s_control[1].taken = 0;
}

int32_t ModApi_SetTeamControl(int32_t team, int32_t cpu)
{
    if (team < 0 || team > 1 || !ModApi_OfflineMatch())
        return 1;
    TeamGeneralInfo *t = team_info(team);
    if (cpu) {
        if (!s_control[team].taken) {
            s_control[team].taken = 1;
            s_control[team].playerNumber = t->playerNumber;
        }
        t->playerNumber = 0;
    } else if (s_control[team].taken) {
        t->playerNumber = s_control[team].playerNumber;
        s_control[team].taken = 0;
    }
    return 0;
}

int32_t ModApi_TeamControl(int32_t team)
{
    if (team < 0 || team > 1 || !ModApi_InMatch())
        return MODSIDE_CPU;
    const TeamGeneralInfo *t = team_info(team);
    const int32_t n = t->playerNumber ? t->playerNumber : t->playerCoachNumber;
    return n == 1 ? MODSIDE_PLAYER1 : n == 2 ? MODSIDE_PLAYER2 : MODSIDE_CPU;
}

/* ---- the next match's pitch ---- */
static int8_t s_nextPitch;

int32_t ModApi_SetMatchPitch(int32_t number)
{
    if (number < 0 || number > 6 || rigs_or_online())
        return 1;
    s_nextPitch = (int8_t)number;
    return 0;
}

/* SetPitchTypeAndNumber (highlight.c, through mod_match.c ModMatch_PitchNumber): the pitch to play,
 * 1..6, 0 the one drawn -- never online or in a rig. */
int16_t ModExtras_PitchNumber(void)
{
    return s_nextPitch && !rigs_or_online() ? s_nextPitch : 0;
}

int32_t ModApi_PitchNumber(void)
{
    extern uint16_t _pitchNumberAndType;
    return ModApi_InMatch() ? (int32_t)(_pitchNumberAndType >> 8) + 1 : 0;
}

void ModExtras_MatchEnded(void)
{
    ModExtras_RestoreControl();
    s_nextPitch = 0;
}

/* ---- PLAY MATCH pressed (play_match_menu.c PlayMatchSelected) ---- */
enum { PLAY_MATCH_FNS = 8 };
static struct { char slug[32]; void (*fn)(void); } s_playMatch[PLAY_MATCH_FNS];
static int32_t s_playMatchCount;

int32_t ModApi_OnPlayMatch(const char *slug, void (*fn)(void))
{
    if (!slug || !fn || s_playMatchCount >= PLAY_MATCH_FNS)
        return 1;
    for (int32_t i = 0; i < s_playMatchCount; i++)
        if (strcmp(s_playMatch[i].slug, slug) == 0)
            return 1;
    snprintf(s_playMatch[s_playMatchCount].slug, sizeof s_playMatch[0].slug, "%s", slug);
    s_playMatch[s_playMatchCount].fn = fn;
    s_playMatchCount++;
    return 0;
}

void ModExtras_PlayMatchPressed(void)
{
    if (rigs_or_online() || ModMatch_Active())
        return;
    for (int32_t i = 0; i < s_playMatchCount; i++)
        s_playMatch[i].fn();
}

/* ---- the connection ---- */
int32_t ModApi_NetStats(ModNetStats *out)
{
    int32_t ping = -1, jitter = 0, local = 0, remote = 0, delay = 0;
    uint32_t rollbacks = 0, holds = 0, frame = 0;
    if (!out)
        return 0;
    memset(out, 0, sizeof *out);
    if (!NetplayMatch_Stats(&ping, &jitter, &local, &remote, &delay, &rollbacks, &holds, &frame,
                            out->rollbacksBySize))
        return 0;
    out->resimulating = (uint8_t)(NetplayMatch_Resimulating() != 0);
    out->pingMs = (int16_t)ping;
    out->jitterMs = (int16_t)jitter;
    out->localBehindX10 = (int16_t)local;
    out->remoteBehindX10 = (int16_t)remote;
    out->delay = (int16_t)delay;
    out->rollbacks = rollbacks;
    out->holds = holds;
    out->frame = frame;
    return 1;
}

#else   /* the Amiga: none of the mods that use these is built there */

int32_t ModApi_OfflineMatch(void) { return 0; }
int32_t ModApi_AnyModMatch(void) { return 0; }
int32_t ModApi_Ball(ModBall *out) { (void)out; return 0; }
int32_t ModApi_SpriteKind(const struct Sprite *sprite) { (void)sprite; return MODSPRITE_OTHER; }
int32_t ModApi_SetTeamControl(int32_t team, int32_t cpu) { (void)team; (void)cpu; return 1; }
int32_t ModApi_TeamControl(int32_t team) { (void)team; return MODSIDE_CPU; }
int32_t ModApi_SetMatchPitch(int32_t number) { (void)number; return 1; }
int32_t ModApi_PitchNumber(void) { return 0; }
int32_t ModApi_OnPlayMatch(const char *slug, void (*fn)(void)) { (void)slug; (void)fn; return 1; }
int32_t ModApi_NetStats(ModNetStats *out) { (void)out; return 0; }
void ModExtras_RestoreControl(void) { }
void ModExtras_KickOff(void) { }
void ModExtras_MatchEnded(void) { }
int16_t ModExtras_PitchNumber(void) { return 0; }

#endif
