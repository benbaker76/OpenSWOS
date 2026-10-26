/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "tackle_foul.h"
#include "test_rules.h"
#include "dos_sound.h"
#include "contest_log.h"    /* how long a slide leaves a player down (ContestLog_Down) */

extern int16_t  _gsPlayerDownTimer;   /* Amiga 50 / DOS 55 — dos_gameplay.c */

/* Card types (not in game_types.h) */
#define CARD_YELLOW                 1
#define CARD_RED                    2
#define CARD_SECOND_YELLOW          3

/* Tackle state */
#define TS_GOOD_TACKLE              2

/* ---- External globals ---- */
extern Sprite   _ballSprite;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;

extern int16_t  _kDefaultDestinations[];     /* 8 direction deltas: dx,dy pairs */
extern uint16_t _ingameTeamPlayerOffsets[];

extern int16_t  _gameState;
extern int16_t  _gameStatePl;
extern int16_t  _breakCameraMode;
extern int16_t  _gameNotInProgressCounterWriteOnly;
extern int16_t  _stoppageTimerTotal;
extern int16_t  _stoppageTimerActive;
extern int16_t  _cameraXVelocity;
extern int16_t  _cameraYVelocity;
extern int16_t  _cameraDirection;
extern uint8_t  _playerTurnFlags;
extern int16_t  _foulXCoordinate;
extern int16_t  _foulYCoordinate;
extern int16_t  _forceLeftTeam;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;

extern int16_t  _plgD3Param;
extern int16_t  _whichCard;
extern int16_t  _refTimer;
extern TeamGeneralInfo *_lastTeamBooked;
extern Sprite  *_bookedPlayer;
extern int16_t  _team1NumAllowedInjuries;
extern int16_t  _team2NumAllowedInjuries;

extern int16_t  _cardsDisallowed;
extern int16_t  _trainingGame;
extern int16_t  _gameLengthInGame;
extern uint16_t _currentGameTick;
extern int16_t  _playerCardChance;
extern void    *_nearestTeammatePtr;

extern int16_t  _ballNextGroundX;
extern int16_t  _ballNextYGroundY;

/* Inline data tables from SWOS2.s */
extern int16_t  _kPlayerTacklingDownTime[];
extern int16_t  _kComputerTacklingDownTime[];
extern uint8_t  _cardAnimStatesNoFlag[];
extern uint8_t  _cardAnimStatesWithFlag[];

/* Animation tables */
extern AnimTable_20 _playerTackledAnimTable;
extern AnimTable    _plTacklingAnimTable;
extern AnimTable    _goalieCatchingBallAnimTable;
extern int16_t  _kGoalkeeperCatchSpeed;
extern int16_t  _kPlayerTacklingSpeed;

/* Injury lookup tables from SWOS2.s */
extern uint8_t  _kInjuryLevels[];
extern uint8_t  _kInjuryLevelAlreadyInjured[];
extern int16_t  _kInjuryRecoveryTime[];
extern uint8_t  _kTackleInjuryProbability[];
extern uint8_t  _kTackleInjuryProbabilityAlreadyInjured[];

/* ---- External functions ---- */
extern void     PlayKickSample(void);
extern void     PlayFoulWhistleSample(void);
extern void     PlayPenalty(void);
extern void     ResetBothTeamSpinTimers(void);
extern void     StopAllPlayers(void);
extern void     ActivateReferee(void);
extern uint16_t Rand(void);
extern PlayerInfo *GetPlayerPointerFromShirtNumber(Sprite *player, TeamGeneralInfo *team);
extern void     SetPlayerAnimationTable(AnimTable *animTable, Sprite *sprite);

/* ========================================================================
 * PlayerTackledTheBallWeak
 * Weak tackle: ball direction adjusted by input, speed = 50% player + 50%,
 * ball speed = 150% of player speed. Marks as good tackle if opponent far.
 * ======================================================================== */
void __attribute__((used, externally_visible))
PlayerTackledTheBallWeak(Sprite *player, TeamGeneralInfo *team)
{
    Sprite *ball = &_ballSprite;
    int16_t inputDir = team->currentAllowedDirection;
    int16_t playerDir, diff, finalDir;
    int16_t dx, dy;
    int16_t speed;
    TeamGeneralInfo *oppTeam;
    Sprite *oppControlled;

    team->passInProgress = 0;

    if (inputDir < 0)
        inputDir = player->direction;

    playerDir = player->direction;
    diff = (int16_t)((playerDir - inputDir) & 0xFFFF);

    if (diff == 0 || (diff & 7) == 4) {
        finalDir = playerDir;
    } else if ((diff & 7) < 4) {
        finalDir = playerDir - 1;
    } else {
        finalDir = playerDir + 1;
    }
    finalDir &= 7;

    team->controlledPlDirection = (int16_t)finalDir;

    /* Set ball destination from direction table */
    dx = _kDefaultDestinations[finalDir * 2];
    dy = _kDefaultDestinations[finalDir * 2 + 1];
    ball->destX = ball->x.s.i + dx;
    ball->destY = ball->y.s.i + dy;

    /* Ball speed: player speed * 0.5, then set = (remaining + half) = 1.5x remaining */
    speed = (uint16_t)player->speed;
    speed >>= 1;
    player->speed = (int16_t)((uint16_t)player->speed - speed);
    player->speed = (int16_t)((uint16_t)player->speed | 1);
    speed = (uint16_t)player->speed;
    ball->speed = (int16_t)(speed + (speed >> 1));

    /* Mark as basic tackle */
    player->tackleState = (int16_t)1;

    /* Check if opponent's controlled player is far enough away for a "good tackle" */
    oppTeam = team->opponentsTeam;
    oppControlled = oppTeam->controlledPlayer;
    if (oppControlled != (Sprite *)0) {
        if (oppControlled->ballDistance >= 9) {
            int16_t dxp = player->x.s.i - oppControlled->x.s.i;
            int16_t dyp = player->y.s.i - oppControlled->y.s.i;
            int32_t distSq = (int32_t)dxp * dxp + (int32_t)dyp * dyp;
            if (distSq > 32) {
                player->tackleState = (int16_t)TS_GOOD_TACKLE;
            }
        }
    }

    PlayKickSample();
    ResetBothTeamSpinTimers();
}

/* ========================================================================
 * SetPlayerDowntimeAfterTackle
 * Sets how long a player lies on the ground based on tackling skill.
 * ======================================================================== */
void __attribute__((used, externally_visible))
SetPlayerDowntimeAfterTackle(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *pi = GetPlayerPointerFromShirtNumber(player, team);
    int16_t *table;
    int16_t tackleSkill;
    int16_t downtime;

    if ((uint16_t)player->tacklingTimer == 0xFFFF)
        table = _kComputerTacklingDownTime;
    else
        table = _kPlayerTacklingDownTime;

    tackleSkill = pi->tackling;
    downtime = table[tackleSkill];
    player->playerDownTimer = (int8_t)(uint8_t)downtime;
    CONTEST_LOG(ContestLog_Down(team, player, downtime));
}

/* ========================================================================
 * PlayerTackled
 * Injury resolution: probabilistic injury based on game length, tackling skill,
 * existing injuries. Sets PL_TACKLED state.
 * ======================================================================== */
void __attribute__((used, externally_visible))
PlayerTackled(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *pi;
    uint8_t *probTable;
    uint8_t *sevTable;
    int16_t teamNum;
    int16_t gameLen;
    int16_t injBitfield;
    uint16_t roll;
    int16_t severity;
    int16_t i;

    /* Training game: 25% chance of any injury processing */
    if (_trainingGame != 0) {
        roll = Rand();
        if ((roll & 3) != 0)
            goto set_tackled;
    }

    /* Check injury quota for this team */
    teamNum = team->teamNumber;
    if (teamNum == 1) {
        if (_team1NumAllowedInjuries == 0)
            goto set_tackled;
    } else {
        if (_team2NumAllowedInjuries == 0)
            goto set_tackled;
    }

    /* Get player info and check existing injury status */
    {
        int16_t ordinal = (uint16_t)player->playerOrdinal - 1;
        uint16_t offset = _ingameTeamPlayerOffsets[ordinal];
        pi = (PlayerInfo *)((uint8_t *)team->inGameTeamPtr + offset + 42);
    }

    gameLen = _gameLengthInGame;
    probTable = _kTackleInjuryProbability;

    injBitfield = pi->injuriesBitfield & 0xE0;
    if (injBitfield == 0x20) {
        probTable = _kTackleInjuryProbabilityAlreadyInjured;
    }

    /* Roll for injury */
    roll = Rand();
    if ((uint8_t)roll >= probTable[gameLen])
        goto set_tackled;

    /* Injury! Determine severity bucket */
    sevTable = _kInjuryLevels;
    injBitfield = pi->injuriesBitfield & 0xE0;
    if (injBitfield == 0x20) {
        sevTable = _kInjuryLevelAlreadyInjured;
    }

    roll = Rand();
    roll &= 0x3F;
    severity = 0x20;

    for (i = 0; i < 7; i++) {
        if ((uint8_t)roll < sevTable[i])
            goto apply_injury;
        roll -= sevTable[i];
        severity += 0x20;
    }
    /* Fall through: max severity */

apply_injury:
    pi->isInjured = 1;
    DOS_SOUND(DOS_EV_INJURY, team->playerNumber != 0);

    /* Only upgrade injury severity, never downgrade */
    if ((uint8_t)severity > (uint8_t)pi->injuriesBitfield) {
        pi->injuriesBitfield = (int8_t)(uint8_t)severity;
    }

    /* Map severity to recovery time */
    {
        int16_t sevIdx = (severity & 0xE0) >> 5;
        player->injuryLevel = (int16_t)_kInjuryRecoveryTime[sevIdx];
    }

    /* Decrement injury quota for team */
    if (teamNum == 1)
        _team1NumAllowedInjuries--;
    else
        _team2NumAllowedInjuries--;

set_tackled:
    player->playerState = (uint8_t)PL_TACKLED;
    player->playerDownTimer = (int8_t)0x32;
    SetPlayerAnimationTable((AnimTable *)&_playerTackledAnimTable, player);
}

/* ----------------------------------------------------------------------------
 * [gameplay] cpu_cards (SWOS.ini) / --cpu-cards=<on|off>.
 *
 * The ORIGINAL never books or sends off a team with no human controller:
 * TryBookingThePlayer and TrySendingOffThePlayer both test playerNumber /
 * playerCoachNumber and bail out (SWOS2.s:26026, both releases) -- the
 * computer fouls you all match and collects nothing for it. Default 1: a CPU
 * side is carded by the same rolls as a human one. 0 restores the original
 * rule.
 *
 * Offline only in effect: an online match has a human on both sides, so the
 * gate this flag bypasses never fires there -- nothing travels in the host's
 * rules. The .RGD replay corpus pins it to 0 (main.c, beside the fire-hold
 * lockout pin): its recordings were made against the original. The Amiga
 * target reads no SWOS.ini and always plays the default. Constant through a
 * match, so it is not match state.
 * ---------------------------------------------------------------------------- */
int _gCpuTeamsGetCards = 1;

/* The OPTIONS menu's "CPU CARDS" row draws from this uint16_t mirror (the
 * string-table header reads its index as one -- see _gameplayStyleOption in
 * dos_gameplay.c for why it cannot point at the `int`). ChangeCpuCards
 * (menus.c) and OptionsMenuBeforeDraw keep the two in step; main.c seeds
 * both from SWOS.ini on the SDL build. */
uint16_t _cpuCardsOption = 1;

/* ========================================================================
 * TryBookingThePlayer
 * Returns 0 if card was given, 1 if not eligible.
 * ======================================================================== */
int16_t __attribute__((used, externally_visible))
TryBookingThePlayer(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *pi;
    int16_t cards;
    int16_t ordinal;
    uint16_t offset;
    TeamStatisticsData *stats;

    if (TestRules_NoInjuriesCards())
        return 1;  /* the testers' rule (test_rules.h): never book — eligibility=no */

    if (_gCpuTeamsGetCards == 0 &&
        team->playerNumber == 0 &&
        team->playerCoachNumber == 0) {
        /* The original's rule ([gameplay] cpu_cards = false; the block over
         * _gCpuTeamsGetCards): a computer side is never booked... */
        if (_plgD3Param != 0) {
            /* ...except in highlight mode, where its first card shows */
            if ((uint16_t)player->cards != 0)
                return 1; /* not eligible */
        } else {
            return 1; /* not eligible */
        }
    }

    /* If player already has a card, check injury quota for second yellow / red */
    if ((uint16_t)player->cards != 0) {
        if (team->teamNumber == 1) {
            if (_team1NumAllowedInjuries == 0)
                return 1;
        } else {
            if (_team2NumAllowedInjuries == 0)
                return 1;
        }
    }

    /* Resolve player in-game slot */
    ordinal = (uint16_t)player->playerOrdinal - 1;
    offset = _ingameTeamPlayerOffsets[ordinal];
    pi = (PlayerInfo *)((uint8_t *)team->inGameTeamPtr + offset + 42);

    cards = (uint16_t)player->cards;
    cards++;

    if (_plgD3Param == 0) {
        /* Normal mode: use LUT based on previous card history */
        uint8_t *lutTable;
        if (pi->previousCards == 0)
            lutTable = _cardAnimStatesNoFlag;
        else
            lutTable = _cardAnimStatesWithFlag;
        pi->numYellowCards = lutTable[cards];
    } else {
        /* Highlight mode: simplified card state */
        if (pi->numYellowCards == 0)
            pi->numYellowCards = 1;
        else
            pi->numYellowCards = 3;
    }

    /* Commit booking */
    player->cards = (int16_t)((uint16_t)player->cards + 1);
    _lastTeamBooked = team;
    _bookedPlayer = player;
    _refTimer = 0;

    if ((uint16_t)player->cards == 2) {
        /* Second yellow = red card (second yellow) */
        _whichCard = CARD_SECOND_YELLOW;
        stats = team->teamStatsPtr;
        stats->bookings--;
        stats->sendingsOff++;
        if (team->teamNumber == 1)
            _team1NumAllowedInjuries--;
        else
            _team2NumAllowedInjuries--;
        return 0;
    }

    /* First yellow */
    _whichCard = CARD_YELLOW;
    stats = team->teamStatsPtr;
    stats->bookings++;
    return 0;
}

/* ========================================================================
 * TrySendingOffThePlayer
 * Straight red card. Returns 0 if card given, 1 if not eligible.
 * ======================================================================== */
int16_t __attribute__((used, externally_visible))
TrySendingOffThePlayer(Sprite *player, TeamGeneralInfo *team)
{
    PlayerInfo *pi;
    int16_t cards;
    int16_t ordinal;
    uint16_t offset;
    TeamStatisticsData *stats;

    if (TestRules_NoInjuriesCards())
        return 1;  /* the testers' rule (test_rules.h): never send off — eligibility=no */

    /* The original's rule ([gameplay] cpu_cards = false; the block over
     * _gCpuTeamsGetCards): a computer side is never sent off */
    if (_gCpuTeamsGetCards == 0 &&
        team->playerNumber == 0 &&
        team->playerCoachNumber == 0)
        return 1;

    /* Check injury quota */
    if (team->teamNumber == 1) {
        if (_team1NumAllowedInjuries == 0)
            return 1;
    } else {
        if (_team2NumAllowedInjuries == 0)
            return 1;
    }

    /* Resolve player in-game slot */
    ordinal = (uint16_t)player->playerOrdinal - 1;
    offset = _ingameTeamPlayerOffsets[ordinal];
    pi = (PlayerInfo *)((uint8_t *)team->inGameTeamPtr + offset + 42);

    /* d0 = cards ^ 1 + 3 */
    cards = (uint16_t)player->cards;
    cards = (cards ^ 1) + 3;

    if (_plgD3Param == 0) {
        /* Normal mode: use LUT */
        uint8_t *lutTable;
        if (pi->previousCards == 0)
            lutTable = _cardAnimStatesNoFlag;
        else
            lutTable = _cardAnimStatesWithFlag;
        pi->numYellowCards = lutTable[cards];
    } else {
        /* Highlight mode: force card state 3 */
        pi->numYellowCards = 3;
    }

    /* Finalize straight red */
    _lastTeamBooked = team;
    _bookedPlayer = player;
    _refTimer = 0;
    _whichCard = CARD_RED;

    stats = team->teamStatsPtr;
    if ((uint16_t)player->cards == 1) {
        /* Had one yellow, now straight red -- remove yellow from bookings */
        stats->bookings--;
    }
    stats->sendingsOff++;

    if (team->teamNumber == 1)
        _team1NumAllowedInjuries--;
    else
        _team2NumAllowedInjuries--;

    return 0;
}

/* ========================================================================
 * SendPlayerAway
 * Called when player gets a red card -- marks them as sent off.
 * ======================================================================== */
void __attribute__((used, externally_visible))
SendPlayerAway(Sprite *player, TeamGeneralInfo *team)
{
    TeamGame *teamGame = team->inGameTeamPtr;
    int16_t ordinal = (uint16_t)player->playerOrdinal - 1;

    /* If this player is the marked player, clear the mark */
    if (ordinal == teamGame->markedPlayer) {
        teamGame->markedPlayer = -1;
    }

    player->cards = (int16_t)-1;
    player->sentAway = (int16_t)1;
    player->destX = (int16_t)-0x14;   /* -20 */
    player->destY = (int16_t)449;
}

/* ========================================================================
 * PlayerTacklingTestFoul
 * Tests if a tackle resulted in a foul. Orchestrates card issuance, foul
 * state, penalty/free kick. A1=tackling player, A6=tackling player's team.
 * ======================================================================== */
void __attribute__((used, externally_visible))
PlayerTacklingTestFoul(Sprite *player, TeamGeneralInfo *team)
{
    TeamGeneralInfo *oppTeam;
    Sprite *victim;
    int16_t dx, dy;
    int32_t distSq;
    int16_t dirDiff;
    TeamStatisticsData *stats;
    uint16_t roll;

    /* Get opponent's controlled player (victim) */
    oppTeam = team->opponentsTeam;
    victim = oppTeam->controlledPlayer;

    /* Null check */
    if (victim == (Sprite *)0)
        return;

    /* Check distance between tackler and victim */
    dx = player->x.s.i - victim->x.s.i;
    dy = player->y.s.i - victim->y.s.i;
    distSq = (int32_t)(int16_t)dx * (int16_t)dx + (int32_t)(int16_t)dy * (int16_t)dy;
    if (distSq > 32)
        return;

    /* If victim is goalkeeper (ordinal 1), just slow down tackler */
    if ((uint16_t)victim->playerOrdinal == 1) {
        player->speed = (int16_t)((uint16_t)player->speed >> 1);
        player->speed = (int16_t)((uint16_t)player->speed >> 1);
        player->speed = (int16_t)((uint16_t)player->speed | 1);
        return;
    }

    /* Check victim is on pitch bounds */
    if (victim->x.s.i < 81 ||
        victim->y.s.i < 129 ||
        victim->x.s.i > 590 ||
        victim->y.s.i > 769)
        return;

    /* Slow down tackler */
    player->speed = (int16_t)((uint16_t)player->speed >> 1);
    player->speed = (int16_t)((uint16_t)player->speed >> 1);
    player->speed = (int16_t)((uint16_t)player->speed | 1);

    /* Tackle the victim (injury resolution) */
    PlayerTackled(victim, oppTeam);

    /* If ball is far from victim, no foul */
    if (victim->ballDistance > 800)
        return;

    /* Check tackle quality -- clean tackle if tackleState==0 or good tackle */
    if ((uint16_t)player->tackleState != 0) {
        if ((uint16_t)player->tackleState == TS_GOOD_TACKLE) {
            DOS_SOUND(DOS_EV_DANGEROUS_PLAY, 0);
            return;
        }

        /* Check direction difference: within +/-1 means from the side = no foul */
        dirDiff = player->direction - victim->direction;
        if (dirDiff < -1 || dirDiff > 1)
            return;
    }

    /* It's a foul! Increment fouls counter */
    stats = team->teamStatsPtr;
    stats->foulsConceded++;

    /* Check if cards are allowed */
    if (_cardsDisallowed != 0 || _trainingGame != 0)
        goto foul_no_card;

    /* Discipline zone check: victim inside main playing area? */
    {
        int16_t vx = victim->x.s.i;
        int16_t vy = victim->y.s.i;

        if (vx >= 193 && vx <= 478 && vy >= 129 && vy <= 769) {
            /* In the main zone -- check penalty area */
            if (team == &_topTeamData) {
                if (vy <= 216)
                    goto maybe_card;
                /* else fall through to nearest-teammate search */
            } else {
                if (vy >= 682)
                    goto maybe_card;
                /* else fall through to nearest-teammate search */
            }
        }

        /* Search for nearest teammate to a fixed point near goal */
        {
            int16_t refX = 336;
            int16_t refY;
            Sprite *nearest = victim;
            int32_t bestDist;
            Sprite **sprTable;
            int16_t i;

            if (team == &_topTeamData)
                refY = 129;
            else
                refY = 769;

            dx = victim->x.s.i - refX;
            dy = victim->y.s.i - refY;
            bestDist = (int32_t)dx * dx + (int32_t)dy * dy;

            sprTable = team->spritesTable;
            /* Ascending: `movea.l (a0)+,a4` under a counting-down `dbf`. The
             * test here is `<=`, so a tie keeps the HIGHEST index — which is
             * only right if the walk is forward. See UpdateControlledPlayer. */
            for (i = 0; i <= 10; i++) {
                Sprite *mate = sprTable[i];
                if ((uint16_t)mate->sentAway != 0)
                    continue;
                if ((uint16_t)mate->playerOrdinal == 1)
                    continue;
                if (mate == player)
                    continue;
                dx = mate->x.s.i - refX;
                dy = mate->y.s.i - refY;
                {
                    int32_t d = (int32_t)dx * dx + (int32_t)dy * dy;
                    if (d <= bestDist) {
                        bestDist = d;
                        nearest = mate;
                    }
                }
            }

            _nearestTeammatePtr = nearest;

            if (nearest == victim) {
                /* Same player is nearest: swap card probabilities */
                roll = Rand();
                if (roll < 32)
                    goto issue_yellow;
                goto try_red;
            }
        }
    }

maybe_card:
    {
        int16_t tickChance = (_currentGameTick & 0x1E) >> 1;
        if (tickChance >= _playerCardChance)
            goto foul_no_card;
    }

    roll = Rand();
    if (roll < 32)
        goto try_red;

issue_yellow:
    if (TryBookingThePlayer(player, team) != 0)
        goto foul_no_card;
    TestFoulForPenaltyAndFreeKick(victim, team);
    ActivateReferee();
    return;

try_red:
    if (TrySendingOffThePlayer(player, team) != 0)
        goto foul_no_card;
    TestFoulForPenaltyAndFreeKick(victim, team);
    ActivateReferee();
    return;

foul_no_card:
    TestFoulForPenaltyAndFreeKick(victim, team);
}

/* ========================================================================
 * GoalkeeperCaughtTheBall
 * Goalkeeper dived and caught the ball. Sets catching state, animation,
 * speed and destination.
 * ======================================================================== */
void __attribute__((used, externally_visible))
GoalkeeperCaughtTheBall(Sprite *keeper, TeamGeneralInfo *team)
{
    if (team == &_topTeamData)
        keeper->direction = (int16_t)4;
    else
        keeper->direction = (int16_t)0;

    team->goalkeeperDivingLeft = 0;
    SetPlayerAnimationTable(&_goalieCatchingBallAnimTable, keeper);
    keeper->playerState = (uint8_t)PL_GOALIE_CATCHING_BALL;
    keeper->playerDownTimer = (int8_t)15;
    keeper->speed = _kGoalkeeperCatchSpeed;
    keeper->destX = _ballNextGroundX;
    keeper->destY = _ballNextYGroundY;

    /* Clamp Y destination to pitch bounds */
    if (keeper->destY < 137)
        keeper->destY = (int16_t)137;
    if (keeper->destY >= 761)
        keeper->destY = (int16_t)761;
}

/* ========================================================================
 * PlayerTackledTheBallStrong
 * Strong tackle: similar to weak but with optional speed boost (125%).
 * ======================================================================== */
void __attribute__((used, externally_visible))
PlayerTackledTheBallStrong(Sprite *player, TeamGeneralInfo *team)
{
    Sprite *ball = &_ballSprite;
    int16_t inputDir = team->currentAllowedDirection;
    int16_t playerDir, diff, finalDir;
    int16_t dx, dy;
    uint16_t speed;
    TeamGeneralInfo *oppTeam;
    Sprite *oppControlled;

    if (inputDir < 0)
        inputDir = player->direction;

    playerDir = player->direction;
    diff = (int16_t)((playerDir - inputDir) & 0xFFFF);

    if (diff == 0 || (diff & 7) == 4) {
        finalDir = playerDir;
    } else if ((diff & 7) < 4) {
        finalDir = playerDir - 1;
    } else {
        finalDir = playerDir + 1;
    }
    finalDir &= 7;

    team->controlledPlDirection = (int16_t)finalDir;

    /* Set ball destination from direction table */
    dx = _kDefaultDestinations[finalDir * 2];
    dy = _kDefaultDestinations[finalDir * 2 + 1];
    ball->destX = ball->x.s.i + dx;
    ball->destY = ball->y.s.i + dy;

    /* Ball speed: if PLAYERNUMBER flag set, boost to 125%; otherwise same as player */
    if (team->playerNumber != 0) {
        /* Note: original asm duplicates the computation -- faithfully reproduced */
        speed = (uint16_t)player->speed;
        speed = (speed >> 2) + speed;
        ball->speed = (int16_t)speed;
        speed = (uint16_t)player->speed;
        speed = (speed >> 2) + speed;
        ball->speed = (int16_t)speed;
    } else {
        ball->speed = player->speed;
    }

    /* Halve player speed */
    player->speed = (int16_t)((uint16_t)player->speed >> 1);

    /* Mark as basic tackle */
    player->tackleState = (int16_t)1;

    /* Check if opponent controlled player is far enough for "good tackle" */
    oppTeam = team->opponentsTeam;
    oppControlled = oppTeam->controlledPlayer;
    if (oppControlled != (Sprite *)0) {
        if (oppControlled->ballDistance >= 9) {
            int16_t dxp = player->x.s.i - oppControlled->x.s.i;
            int16_t dyp = player->y.s.i - oppControlled->y.s.i;
            int32_t distSq = (int32_t)dxp * dxp + (int32_t)dyp * dyp;
            if (distSq > 32) {
                DOS_SOUND(DOS_EV_GOOD_TACKLE, 0);
                player->tackleState = (int16_t)TS_GOOD_TACKLE;
            }
        }
    }

    PlayKickSample();
    ResetBothTeamSpinTimers();
}

/* ========================================================================
 * TestFoulForPenaltyAndFreeKick
 * Determines foul type (penalty, free kick zones, or simple foul) based on
 * victim position. Sets game state, foul coords, and enters stopped state.
 * ======================================================================== */
void __attribute__((used, externally_visible))
TestFoulForPenaltyAndFreeKick(Sprite *victim, TeamGeneralInfo *team)
{
    int16_t vx, vy;

    if (_gameStatePl == ST_STOPPED)
        return;

    PlayFoulWhistleSample();
    /* nullsub_6 omitted */

    vx = victim->x.s.i;
    vy = victim->y.s.i;

    if (team == &_topTeamData) {
        /* Top team fouled -- camera direction 0 (up) */
        _cameraDirection = 0;

        /* Check penalty area: y <= 216, 193 <= x <= 478 */
        if (vy <= 216 && vx >= 193 && vx <= 478) {
            _gameState = ST_PENALTY;
            _breakCameraMode = -1;
            _playerTurnFlags = 0x83;
            _foulXCoordinate = 336;
            _foulYCoordinate = 187;
            PlayPenalty();
            goto apply_break;
        }

        DOS_SOUND(DOS_EV_FOUL, 0);
        /* Free kick band: 216 < y < 331 */
        if (vy < 216 || vy >= 331)
            goto set_foul;
        goto select_fk_quadrant;
    } else {
        /* Bottom team fouled -- camera direction 4 (down) */
        _cameraDirection = 4;

        /* Check penalty area: y >= 682, 193 <= x <= 478 */
        if (vy >= 682 && vx >= 193 && vx <= 478) {
            _gameState = ST_PENALTY;
            _breakCameraMode = -1;
            _playerTurnFlags = 0x38;
            _foulXCoordinate = 336;
            _foulYCoordinate = 711;
            PlayPenalty();
            goto apply_break;
        }

        DOS_SOUND(DOS_EV_FOUL, 0);
        /* Free kick band: 567 <= y <= 682 */
        if (vy < 567 || vy > 682)
            goto set_foul;
    }

select_fk_quadrant:
    if (team == &_bottomTeamData) {
        /* Bottom team: mirrored quadrant assignment */
        if (vx < 153) goto fk_right3;
        if (vx < 261) goto fk_right2;
        if (vx < 309) goto fk_right1;
        if (vx < 362) goto fk_center;
        if (vx < 410) goto fk_left3;
        if (vx < 518) goto fk_left2;
        goto fk_left1;
    } else {
        /* Top team */
        if (vx < 153) goto fk_left1;
        if (vx < 261) goto fk_left2;
        if (vx < 309) goto fk_left3;
        if (vx < 362) goto fk_center;
        if (vx < 410) goto fk_right1;
        if (vx < 518) goto fk_right2;
        goto fk_right3;
    }

fk_left1:  _gameState = ST_FREE_KICK_LEFT1;  goto commit_foul;
fk_left2:  _gameState = ST_FREE_KICK_LEFT2;  goto commit_foul;
fk_left3:  _gameState = ST_FREE_KICK_LEFT3;  goto commit_foul;
fk_center: _gameState = ST_FREE_KICK_CENTER; goto commit_foul;
fk_right1: _gameState = ST_FREE_KICK_RIGHT1; goto commit_foul;
fk_right2: _gameState = ST_FREE_KICK_RIGHT2; goto commit_foul;
fk_right3: _gameState = ST_FREE_KICK_RIGHT3; goto commit_foul;

set_foul:
    _gameState = ST_FOUL;

commit_foul:
    _breakCameraMode = -1;
    _playerTurnFlags = 0xFF;
    _foulXCoordinate = vx;
    _foulYCoordinate = vy;

apply_break:
    if (_forceLeftTeam == 1)
        team = &_bottomTeamData;

    _gameStatePl = ST_STOPPED;
    _gameNotInProgressCounterWriteOnly = 0;
    _lastTeamPlayedBeforeBreak = team->opponentsTeam;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    StopAllPlayers();
    _cameraXVelocity = 0;
    _cameraYVelocity = 0;
}


/* ========================================================================
 * PlayerBeginTackling
 *   from: PlayerBeginTackling
 *   in:  D0=pressedDir, A1=player, A6=team
 *
 * Sets player state to tackling, assigns animation, direction, speed,
 * and destination. Checks fasterTackle player attribute (currently dead
 * code in original: downtime always overwritten with 0xFF).
 * ======================================================================== */
void __attribute__((used, externally_visible))
PlayerBeginTackling(int16_t pressedDir, Sprite *player, TeamGeneralInfo *team)
{
    player->tackleState = 0;
    team->controlledPlDirection = pressedDir;
    player->direction = pressedDir;

    SetPlayerAnimationTable(&_plTacklingAnimTable, player);
    player->playerState = (uint8_t)PL_TACKLING;
    player->playerDownTimer = (int8_t)_gsPlayerDownTimer;   /* Amiga 50 / DOS 55 */

    /* Look up player info to check fasterTackle */
    {
        int16_t ordinal = player->playerOrdinal - 1;
        uint16_t offset = _ingameTeamPlayerOffsets[ordinal];
        uint8_t *pihBase = (uint8_t *)team->inGameTeamPtr + offset;
        PlayerInfo *pi = (PlayerInfo *)(pihBase + 42);
        if (pi->field_8 != 0) {
            player->playerDownTimer = (int8_t)25;
        }
    }

    /* Original asm always overwrites with 0xFF here */
    player->playerDownTimer = (int8_t)(uint8_t)0xFF;

    /* Set destination from kDefaultDestinations */
    int16_t destIdx = pressedDir * 2;
    player->destX = player->x.s.i + _kDefaultDestinations[destIdx];
    player->destY = player->y.s.i + _kDefaultDestinations[destIdx + 1];

    player->speed = _kPlayerTacklingSpeed;
    player->tacklingTimer = 0;
}
