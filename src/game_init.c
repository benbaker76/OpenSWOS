/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "game_init.h"
#include "dos_gameplay.h"   /* ApplyGameStyle — DOS/Amiga match tuning */
#include "endian_helpers.h"
#include "game_types.h"
#include "play_match_menu.h"
#include "string_utils.h"
#include "shooting_practice.h"   /* shared: compiled for both targets */
#include "penalty_shootout.h"    /* shared: compiled for both targets */
#include "challenge.h"           /* a saved position skips the pre-match prompts */
#ifdef SWOS_SDL_BUILD
#include "uniform.h"        /* a kit chosen for this match (UNIFORM screen) */
#endif

/* ---- External globals (in SWOS2.s code/BSS sections) ---- */
extern uintptr_t _currentScorer;
extern Sprite  *_lastPlayerBeforeGoalkeeper;
extern uint16_t _goalScored;
extern uint16_t _runSlower;
extern uint16_t _whichCard;
extern Sprite  *_bookedPlayer;
extern uint16_t _playerHadBall;
extern uint32_t _lastKeeperPlayed;
extern TeamGeneralInfo *_lastTeamPlayed;
extern Sprite  *_lastPlayerPlayed;
extern uint16_t _penalty;
extern volatile uint16_t _goalCameraMode;
extern uint16_t _goalOut;
extern uint16_t _gameNotInProgressCounterWriteOnly;
extern uint16_t _fireBlocked;
extern TeamGeneralInfo *_lastTeamPlayedBeforeBreak;
extern uint16_t _stoppageTimerTotal;
extern uint16_t _stoppageTimerActive;
extern uint16_t _stoppageEventTimer;
extern uint16_t _inGameCounter;
extern uint16_t _gameStatePl;
extern uint16_t _gameState;
extern uint16_t _breakState;
extern uint16_t _breakCameraMode;
extern uint16_t _teamPlayingUp;
extern uint16_t _topTeamPlayerNo;
extern uint16_t _bottomTeamPlayerNo;
extern uint16_t _topTeamCoachNo;
extern uint16_t _bottomTeamCoachNo;
extern uint16_t _pl1Coach;
extern uint16_t _pl2Coach;
extern TeamGeneralInfo _topTeamData;
extern TeamGeneralInfo _bottomTeamData;
extern Sprite **_team1SpritesTable;
extern Sprite **_team2SpritesTable;
extern TeamGame         *_topTeamPtr;
extern TeamGame         *_bottomTeamPtr;
extern TeamStatisticsData _team1StatsData;
extern TeamStatisticsData _team2StatsData;
extern uint16_t _pl1Tactics;
extern uint16_t _pl2Tactics;

/* InitGameSaveTeams globals */
extern TeamGame  _topTeamInGame;
extern TeamGame  _bottomTeamInGame;
extern TeamGame  _topTeamSaved;
extern TeamGame  _bottomTeamSaved;
extern uint16_t _pitchNumberAndType;
extern uint8_t  _pl1Fire;
extern uint8_t  _pl2Fire;
extern uint16_t _paletteIndex;

/* Chunk 103 externs */
extern uint16_t _trainingGame;
extern uint16_t _plgD3Param;
extern uint16_t _gameMaxSubstitutes;
extern TeamFileEx _careerTeam;
extern uint8_t _currentTeam[];

/* Chunk 104 externs */
extern uint16_t _allPlayerTeamsEqual;
extern uint16_t _avgSelectedTeamsPlayerPrice;
extern uint16_t _gameType;
extern uint16_t _team1SelectedPlayerIndex;
extern uint16_t _team2SelectedPlayerIndex;
extern uint16_t _selTeamSelectedPlayerPosition;
extern uint16_t _currentTeamSelectedPlayerPosition;
extern uint8_t _teamAName[];
extern uint8_t _teamBName[];
extern TeamGame _bottomTeamInGame;

/* ---- External function declarations ---- */
extern void Player1StatusProc(void);
extern void Player2StatusProc(void);
extern void FadeOutToBlack(void);
extern void LoadPitchMap(uint16_t index);
extern void FixupAndColorPitchFile(void);
extern void ConvertColorsAndTextSprites(TeamGame *team1, TeamGame *team2);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex);
extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern uint16_t _cGetPlayerAtIndexFileIdx;
extern uint8_t *StringCopy(const uint8_t *src, uint8_t *dst);
extern uint8_t *ExtractSurname(uint8_t *name, uint16_t maxLenAndFlags);
extern uint16_t IsPlayerInjured(PlayerFile *playerFile, int16_t mode);
extern uint16_t ShouldApplyRandomPlayerStatusAfterSim(void);   /* career_status.c returns uint16_t */
extern void Career_UpdateSinglePlayerPostMatchStats(uint8_t *playerFileBase);
extern uint16_t GetAveragePlayerPrice(TeamFileEx *teamFileBase);
extern uint8_t *GetPlayerInfoBreakPosition(uint8_t *teamPtr, uint16_t requestedSlot,
                                              uint16_t *outSlot, uint16_t *outPlayerIndex);

/* ================================================================
 * InitTeamsData  (SWOS2.s line 19585)
 *
 * Clears all per-match game state globals, sets gameState to
 * ST_GAME_IN_PROGRESS, then initializes topTeamData and
 * bottomTeamData TeamGeneralInfo structs. Runs a 2-iteration
 * loop: first iteration fills the "top" slot, second fills
 * "bottom". If teamPlayingUp != 1, a0/a1 are swapped so the
 * struct that gets filled first is bottomTeamData instead.
 * ================================================================ */
__attribute__((used, externally_visible))
void InitTeamsData(void)
{
    /* DOS-vs-Amiga gameplay tuning: write the per-style physics/timer constants
     * (ball friction/gravity, keeper distances, match-clock + stoppage timers) for
     * the active gDosGameplay. Done per-match so an in-game toggle takes effect on
     * the next match; with DOS gameplay off it writes the Amiga defaults (no-op). */
    ApplyGameStyle();

    /* Clear per-match game state */
    _currentScorer = 0;
    _lastPlayerBeforeGoalkeeper = (Sprite *)0;
    _goalScored = 0;
    _runSlower = 0;
    _whichCard = 0;
    _bookedPlayer = (Sprite *)0;
    _playerHadBall = 0;
    _lastKeeperPlayed = 0;
    _lastTeamPlayed = (TeamGeneralInfo *)0;
    _lastPlayerPlayed = (Sprite *)0;
    _penalty = 0;
    _goalCameraMode = 0;
    _goalOut = 0;
    _gameNotInProgressCounterWriteOnly = 0;
    _fireBlocked = 0;
    _lastTeamPlayedBeforeBreak = (TeamGeneralInfo *)0;
    _stoppageTimerTotal = 0;
    _stoppageTimerActive = 0;
    _stoppageEventTimer = 0;
    _inGameCounter = 0;
    _gameStatePl = ST_GAME_IN_PROGRESS;
    _gameState = ST_GAME_IN_PROGRESS;
    _breakState = 0;
    _breakCameraMode = 0xFFFF;

    /* 2-iteration loop: first for top team (d7=1, d5=1), second for bottom (d7=0, d5=2) */
    uint16_t          playerNo    = _topTeamPlayerNo;
    uint16_t          coachNo     = _topTeamCoachNo;
    uint16_t          isPlCoach   = _pl1Coach;
    TeamGeneralInfo  *a0          = &_topTeamData;       /* destination struct */
    TeamGeneralInfo  *a1          = &_bottomTeamData;    /* opponent struct */
    Sprite          **spritesTab  = _team1SpritesTable;
    TeamGame         *teamPtr     = _topTeamPtr;
    TeamStatisticsData *statsPtr  = &_team1StatsData;
    uint16_t         *tactics     = &_pl1Tactics;
    uint16_t          teamNum     = 1;

    if (_teamPlayingUp != 1) {
        /* swap a0/a1 */
        TeamGeneralInfo *tmp = a0;
        a0 = a1;
        a1 = tmp;
    }

    int16_t iter;
    for (iter = 1; iter >= 0; iter--) {
        /* Fill struct at a0 */
        a0->opponentsTeam                      = a1;
        a0->inGameTeamPtr                      = teamPtr;
        a0->teamStatsPtr                       = statsPtr;
        a0->playerNumber                       = (int16_t)playerNo;
        a0->playerCoachNumber                   = (int16_t)coachNo;
        a0->isPlCoach                           = (int16_t)isPlCoach;
        a0->spritesTable                       = spritesTab;
        a0->teamNumber                         = (int16_t)teamNum;
        a0->tactics                            = (int16_t)*tactics;
        a0->goalkeeperPlaying                  = 0;
        a0->resetControls                      = 0;
        a0->updatePlayerIndex                  = 10;
        a0->controlledPlayer                   = 0;
        a0->passToPlayerPtr                    = 0;
        a0->passingKickingPlayer               = 0;
        a0->playerHasBall                      = 0;
        a0->goalkeeperDivingRight              = 0;
        a0->goalkeeperDivingLeft               = 0;
        a0->ballOutOfPlayOrKeeper              = 0;
        a0->goaliePlayingOrOut                 = 0;
        a0->passingBall                        = 0;
        a0->passingToPlayer                    = 0;
        a0->playerSwitchTimer                  = 0;
        a0->ballInPlay                         = 0;
        a0->ballOutOfPlay                      = 0;
        a0->passKickTimer                      = 0;
        a0->ballCanBeControlled                = 0;
        a0->field_72                           = 0;
        a0->ballControllingPlayerDirection     = (int16_t)0xFFFF;
        a0->field_74                           = 0;
        a0->wonTheBallTimer                    = 0;
        a0->spinTimer                          = (int16_t)0xFFFF;
        a0->field_3C                           = 0;
        a0->goalkeeperSavedCommentTimer        = 0;
        a0->lastHeadingTacklingPlayer          = 0;
        a0->field_4E                           = 0;
        a0->headerOrTackle                     = 0;
        a0->shooting                           = 0;
        a0->passInProgress                     = 0;

        /* Set up for second iteration (bottom team) */
        playerNo    = _bottomTeamPlayerNo;
        coachNo     = _bottomTeamCoachNo;
        isPlCoach   = _pl2Coach;
        a0          = &_bottomTeamData;
        a1          = &_topTeamData;
        spritesTab  = _team2SpritesTable;
        teamPtr     = _bottomTeamPtr;  /* bottom team pointer */
        statsPtr    = &_team2StatsData;
        tactics     = &_pl2Tactics;
        teamNum     = 2;

        if (_teamPlayingUp != 1) {
            /* swap a0/a1 for second iteration too */
            TeamGeneralInfo *tmp = a0;
            a0 = a1;
            a1 = tmp;
        }
    }
}

/* ================================================================
 * InitGameSaveTeams  (SWOS2.s line 11941)
 *
 * Saves both team in-game structs (1018 bytes each) to snapshot
 * buffers, stores team pointers, loads pitch, converts colors
 * and sprites, waits for both fire buttons released, fades out,
 * sets paletteIndex=2.
 *
 * NOTE: In the original asm, this function falls through into
 * InitGameRestoreTeams via "bra.w *+4". The asm stub handles
 * that fall-through; this C function only covers the save portion.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InitGameSaveTeams(TeamGame *leftTeam, TeamGame *rightTeam)
{
    /* Copy both team structs (1018 bytes each) to snapshot buffers */
    uint8_t *src1 = (uint8_t *)&_topTeamInGame;
    uint8_t *dst1 = (uint8_t *)&_topTeamSaved;
    uint8_t *src2 = (uint8_t *)&_bottomTeamInGame;
    uint8_t *dst2 = (uint8_t *)&_bottomTeamSaved;
    int16_t i;
    for (i = 1017; i >= 0; i--) {
        *dst1++ = *src1++;
        *dst2++ = *src2++;
    }

    /* Save team pointers */
    _topTeamPtr = leftTeam;
    _bottomTeamPtr = rightTeam;

    /* Load pitch map (high byte of pitchNumberAndType is the index) */
    LoadPitchMap(_pitchNumberAndType >> 8);
    FixupAndColorPitchFile();
    ConvertColorsAndTextSprites(leftTeam, rightTeam);

    /* Wait for both fire buttons released.
     *
     * On Amiga Player1StatusProc / Player2StatusProc read hardware
     * joystick registers (JOY1DAT, CIAA_PRA) directly — the controller
     * updates them in real time so this tight spin sees fire release as
     * soon as the user lets go.
     *
     * On SDL joy1Dat / ciaa_pra are software globals updated by
     * InputSDL_PostFrame inside Platform_VBlank. Without
     * WaitForVSync inside the spin, no event pump runs and the fire
     * bits never clear — the program hangs here whenever the user is
     * still holding fire (e.g. they pressed VIEW HIGHLIGHTS and held
     * the key for any noticeable time). Tester reproducer: hold
     * right-Ctrl after clicking VIEW HIGHLIGHTS on Windows.
     *
     * Fix: drive WaitForVSync each iteration on SDL so PostFrame runs
     * and the fire state can update. The hold doubles as the
     * stadium-screen visible duration the original Amiga gets for
     * free from the same release-wait. */
#ifdef SWOS_SDL_BUILD
    {
        extern void WaitForVSync(void);
        for (;;) {
            WaitForVSync();
            Player1StatusProc();
            if (_pl1Fire)
                continue;
            Player2StatusProc();
            if (!_pl2Fire)
                break;
        }
    }
#else
    for (;;) {
        Player1StatusProc();
        if (_pl1Fire)
            continue;
        Player2StatusProc();
        if (!_pl2Fire)
            break;
    }
#endif

    FadeOutToBlack();
    _paletteIndex = 2;
}

/* ================================================================
 * Chunk 103 — Game Init Player Utilities
 * ================================================================ */

/* ================================================================
 * UpdatePlayerInjuries  (SWOS2.s line 31278)
 *
 * Copies injury bitfield from in-game PlayerInfo back to the
 * PlayerFile in the team file for non-computer teams.
 *
 * Asm: a1 = TeamGame ptr, a2 = TeamFile ptr
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdatePlayerInjuries(TeamGame *tg, TeamFileEx *teamFileBase)
{
    if (teamFileBase->team.teamControls == 1)  /* TEAM_COMPUTER */
        return;

    for (int16_t i = 15; i >= 0; i--) {
        PlayerInfo *pi = &tg->players[i];
        if (pi->skip != 0)     /* substituted */
            continue;
        if (pi->isInjured == 0)
            continue;

        /* Get the player in the team file.
         * GetPlayer returns a base pointer; PlayerFile starts sizeof(TeamFile) bytes in. */
        uint16_t teamNum = PACK_BE_U16(teamFileBase->team.countryNumber, teamFileBase->team.teamOrdinal);
        uint8_t *playerBase = GetPlayer(teamNum, (uint16_t)(int8_t)pi->playerIndex);
        PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));

        /* Merge injury bitfield: keep low 5 bits, replace high 3 bits */
        uint8_t injuries = (uint8_t)pi->injuriesBitfield;
        uint8_t cardInj = pf->cardsInjuries;
        cardInj = (uint8_t)((cardInj & 0x1F) | injuries);
        pf->cardsInjuries = cardInj;
    }
}

/* ================================================================
 * AccumulateMatchGoalsIntoTeamFile  (SWOS2.s line 31308)
 *
 * Adds own-goals count to team file, then for each player who
 * scored, accumulates goals into the PlayerFile.goalStats[d7].
 *
 * Asm: a1 = TeamGame ptr, a2 = TeamFile ptr, d7.w = goalStats offset
 * ================================================================ */
__attribute__((used, externally_visible))
void AccumulateMatchGoalsIntoTeamFile(TeamGame *tg, TeamFileEx *teamFileBase,
                                        uint16_t statOffset)
{
    teamFileBase->team.writesZeroHere += (uint8_t)tg->numOwnGoals;

    for (int16_t i = 15; i >= 0; i--) {
        PlayerInfo *pi = &tg->players[i];
        int8_t goals = pi->goalsScored;
        if (goals == 0)
            continue;

        uint16_t teamNum = PACK_BE_U16(teamFileBase->team.countryNumber, teamFileBase->team.teamOrdinal);
        uint8_t *playerBase = GetPlayer(teamNum, (uint16_t)(int8_t)pi->playerIndex);
        PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));
        pf->goalStats[statOffset] += (uint8_t)goals;
    }
}

/* ================================================================
 * SetPlayerNameAndFace  (SWOS2.s line 31434)
 *
 * Copies full player name from PlayerFile to PlayerInfo, and
 * extracts face index from positionAndFace.
 *
 * Asm: a5 = playerBase (from GetPlayer/GetPlayerAtIndex),
 *      a6 = PlayerInfo ptr
 * The asm uses $4C(a5) to reach PlayerFile fields ($4C = 76 =
 * sizeof(TeamFile) = offset to first PlayerFile entry).
 * ================================================================ */
__attribute__((used, externally_visible))
void SetPlayerNameAndFace(uint8_t *playerBase, PlayerInfo *pi)
{
    PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));

    /* Copy full player name */
    StringCopy((uint8_t *)pf->playerName, (uint8_t *)pi->fullName);

    /* Extract face from positionAndFace: bits 3-4 → 0-3 */
    uint16_t face = (uint16_t)(pf->positionAndFace & PF_FACE_MASK) >> 3;
    pi->face2 = (int8_t)face;
}

/* ================================================================
 * InitInGamePlayer  (SWOS2.s line 31455)
 *
 * Initializes an in-game PlayerInfo from the corresponding
 * PlayerFile. Sets short name, face, position, substitution status,
 * cards, injuries, and shirt number.
 *
 * Asm: d1 = player index (d2 from outer loop),
 *      a5 = ptr to TeamFileEx base (raw player data),
 *      a6 = PlayerInfo ptr within TeamGame
 * ================================================================ */
__attribute__((used, externally_visible))
void InitInGamePlayer(uint16_t playerIndex, uint16_t playerSlot,
                        uint8_t *playerBase, PlayerInfo *pi)
{
    PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));

    pi->playerIndex = (int8_t)playerIndex;
    pi->goalsScored = 0;
    pi->halfPlayed = 0;

    /* Extract surname for short name display */
    uint8_t *surname = ExtractSurname((uint8_t *)pf->playerName, 0x800E);
    StringCopy(surname, (uint8_t *)pi->shortName);

    /* Face from positionAndFace: bits 3-4 → 0-3 */
    uint16_t face = (uint16_t)(pf->positionAndFace & PF_FACE_MASK) >> 3;
    pi->face = (int8_t)face;

    /* Position: bits 5-7 of positionAndFace */
    uint16_t pos = (uint16_t)(pf->positionAndFace & PF_POSITION_MASK) >> 5;
    pi->position = (int8_t)pos;

    /* Substituted flag */
    pi->skip = 0;  /* not substituted */

    /* Check if player is injured/unavailable (bench slots 11+) */
    if (playerSlot >= 11) {
        int16_t mode = (int16_t)_plgD3Param;
        uint16_t injured = IsPlayerInjured((PlayerFile *)playerBase, mode);
        if (injured != 0) {
            /* Player is injured — mark as unavailable */
            pi->position = (int8_t)0xFF;
            pi->skip = 1;  /* substituted/unavailable */
        } else {
            /* Not injured but beyond max subs limit */
            uint16_t maxSubs = _gameMaxSubstitutes + 11;
            if (playerSlot >= maxSubs) {
                pi->position = (int8_t)0xFF;
                pi->skip = 1;
            }
        }
    }

    /* Cards from player file */
    pi->numYellowCards = 0;
    uint8_t cardInj = pf->cardsInjuries;
    if (_plgD3Param == 0) {
        /* League/friendly mode: extract card bits 2-4 → shift right 2 */
        uint16_t cards = (uint16_t)(cardInj & 0x1C) >> 2;
        pi->numYellowCards = (int8_t)cards;
    } else {
        /* Career/tournament mode: extract bits 0-1 */
        uint16_t cards = (uint16_t)(cardInj & 0x03);
        if (cards >= 1) {
            pi->numYellowCards = 2;
        }
    }

    pi->previousCards = pi->numYellowCards;
    pi->isInjured = 0;
    pi->field_B = 0;  /* disallowSubs */
    pi->injuriesBitfield = 0;

    /* Copy injury bitfield (high 3 bits of cardsInjuries) */
    pi->injuriesBitfield = (int8_t)(pf->cardsInjuries & 0xE0);

    pi->field_8 = 0;  /* fasterTackle */
    pi->shirtNumber = pf->shirtNumber;
}

/* ================================================================
 * ApplyPlayerStatusAfterSim  (SWOS2.s line 31227)
 *
 * After a simulated match, applies card/injury effects from
 * in-game PlayerInfo back to the PlayerFile.
 *
 * Asm: a1 = TeamGame ptr, a2 = TeamFile ptr
 * ================================================================ */
__attribute__((used, externally_visible))
void ApplyPlayerStatusAfterSim(TeamGame *tg, TeamFileEx *teamFileBase)
{
    if (_trainingGame != 0)
        return;

    if (teamFileBase->team.teamControls == 1)  /* TEAM_COMPUTER */
        return;

    for (int16_t i = 15; i >= 0; i--) {
        PlayerInfo *pi = &tg->players[i];
        if (pi->skip != 0)        /* substituted */
            continue;
        if (pi->numYellowCards == 0)  /* cards field */
            continue;

        uint16_t teamNum = PACK_BE_U16(teamFileBase->team.countryNumber, teamFileBase->team.teamOrdinal);
        uint8_t *playerBase = GetPlayer(teamNum, (uint16_t)(int8_t)pi->playerIndex);

        uint8_t cardVal = (uint8_t)pi->numYellowCards;
        PlayerFile *pf = (PlayerFile *)(playerBase + sizeof(TeamFile));

        if (_plgD3Param == 0) {
            /* League/friendly mode */
            uint16_t shifted = (uint16_t)(cardVal << 2) & 0x1C;
            if (ShouldApplyRandomPlayerStatusAfterSim() == 0) {
                /* Don't apply — but if exactly 4 (one yellow?), skip entirely */
                if (shifted == 4)
                    continue;
                shifted = 8;  /* override to 2 yellows */
            }
            uint8_t cur = pf->cardsInjuries;
            cur = (uint8_t)((cur & ~0x1C) | shifted);
            pf->cardsInjuries = cur;
        } else {
            /* Career/tournament mode */
            if (cardVal == 1)
                continue;  /* no action for 1 */
            cardVal -= 1;
            uint16_t bits = (uint16_t)(cardVal & 0x03);
            uint8_t cur = pf->cardsInjuries;
            cur = (uint8_t)((cur & ~0x03) | bits);
            pf->cardsInjuries = cur;
        }
    }
}

/* ================================================================
 * UpdateCareerPlayerStatsAfterMatch  (SWOS2.s line 31196)
 *
 * For each non-substituted player who played the full match
 * (halfPlayed == 2), calls Career_UpdateSinglePlayerPostMatchStats.
 * Only runs for the career team and not during training.
 *
 * Asm: a1 = TeamGame ptr, a2 = TeamFile ptr
 * ================================================================ */
__attribute__((used, externally_visible))
void UpdateCareerPlayerStatsAfterMatch(TeamGame *tg, TeamFileEx *teamFileBase)
{
    if (_trainingGame != 0)
        return;
    if (teamFileBase != &_careerTeam)
        return;

    for (int16_t i = 15; i >= 0; i--) {
        PlayerInfo *pi = &tg->players[i];
        if (pi->skip != 0)      /* substituted */
            continue;
        if (pi->halfPlayed != 2)  /* didn't play full match */
            continue;

        uint16_t teamNum = PACK_BE_U16(teamFileBase->team.countryNumber, teamFileBase->team.teamOrdinal);
        uint8_t *playerBase = GetPlayer(teamNum, (uint16_t)(int8_t)pi->playerIndex);
        Career_UpdateSinglePlayerPostMatchStats(playerBase);
    }
}

/* ================================================================
 * Chunk 104 — InitInGameTeamStructure
 * ================================================================ */

/* ================================================================
 * InitInGameTeamStructure  (SWOS2.s line 31332)
 *
 * Initializes a TeamGame struct from a TeamFile for match play.
 * For each of 16 players: calls GetPlayerAtIndex, InitInGamePlayer,
 * GetPlayerInfoBreakPosition, SetPlayerNameAndFace, and clears
 * skill fields. Computes team price percentage if needed.
 * Copies team name.
 *
 * Asm: a2=TeamFile*, a4=TeamGame*, a6=int16_t* avgPricePct
 * ================================================================ */
__attribute__((used, externally_visible))
void InitInGameTeamStructure(TeamFileEx *teamFileBase, TeamGame *tg,
                                int16_t *avgPricePct)
{
    TeamFile *tf = &teamFileBase->team;

    tg->markedPlayer = -1;
    *avgPricePct = 100;

    /* Compute team price percentage for non-career, equal-teams mode */
    if (_gameType != 4 /* GAME_TYPE_CAREER */) {
        if (_allPlayerTeamsEqual != 0 && tf->teamControls == 2 /* TEAM_PLAYER_COACH */) {
            uint16_t avgPrice = GetAveragePlayerPrice(teamFileBase);
            uint32_t pct = (uint32_t)_avgSelectedTeamsPlayerPrice * 100;
            pct = pct / avgPrice;
            *avgPricePct = (int16_t)(pct + 6);
        }
    }

    /* Clear playerHasBall in the TeamGeneralInfo that overlays the start of TeamGame.
     * The asm does: movea.l a4,a6; clr.b TEAMGENERALINFO_PLAYERHASBALL(a6)
     * TEAMGENERALINFO_PLAYERHASBALL is at offset 40 in TeamGeneralInfo.
     * But a6 is now used as TeamGame base, and offset 40 = numOwnGoals.
     * This is the asm reusing the TeamGame memory for something else initially. */
    tg->numOwnGoals = 0;

    for (uint16_t d2 = 0; d2 < 16; d2++) {
        uint16_t teamNum = ((uint16_t)tf->countryNumber << 8) | tf->teamOrdinal;
        uint16_t d1 = d2;

        /* In training mode for currentTeam, slots 13+ map to slot 0 */
        if (_trainingGame != 0 && (uint8_t *)teamFileBase == _currentTeam && d1 >= 13) {
            d1 = 0;
        }

        uint8_t *playerBase = GetPlayerAtIndex(teamNum, d1);
        d1 = _cGetPlayerAtIndexFileIdx;  /* GetPlayerAtIndex remaps ordinal → file index in d1 */
        PlayerInfo *pi = &tg->players[d2];

        InitInGamePlayer(d1, d2, playerBase, pi);

        /* GetPlayerInfoBreakPosition: maps slot via tactics, returns mapped slot and file index */
        d1 = d2;  /* reset */
        if (_trainingGame != 0 && (uint8_t *)teamFileBase == _currentTeam && d1 >= 13) {
            d1 = 0;
        }
        uint16_t slot, playerIndex;
        uint8_t *playerBase2 = GetPlayerInfoBreakPosition((uint8_t *)teamFileBase, d1, &slot, &playerIndex);

        /* Check if this player is the selected player (for marking) */
        uint16_t selectedIdx;
        if (tg == &_topTeamInGame) {
            selectedIdx = _team1SelectedPlayerIndex;
        } else {
            selectedIdx = _team2SelectedPlayerIndex;
        }
        if (playerIndex == selectedIdx) {
            tg->markedPlayer = (int16_t)slot;
        }

        /* Also check career/current team selected player position */
        uint16_t selPos = _selTeamSelectedPlayerPosition;
        if (teamFileBase == &_careerTeam) {
            if (playerIndex == selPos) {
                tg->markedPlayer = (int16_t)slot;
            }
        } else if ((uint8_t *)teamFileBase == _currentTeam) {
            selPos = _currentTeamSelectedPlayerPosition;
            if (playerIndex == selPos) {
                tg->markedPlayer = (int16_t)slot;
            }
        }

        /* SetPlayerNameAndFace uses the player ptr returned by GetPlayerInfoBreakPosition */
        SetPlayerNameAndFace(playerBase2, pi);

        /* Clear all skill fields */
        pi->passing = 0;
        pi->shooting = 0;
        pi->heading = 0;
        pi->tackling = 0;
        pi->ballControl = 0;
        pi->speed = 0;
        pi->finishing = 0;
        pi->goalieSkill = 0;
    }

    /* Copy team name */
    uint8_t *nameSource;
    if (_trainingGame != 0) {
        nameSource = _teamAName;
        if (tg != &_topTeamInGame) {
            nameSource = _teamBName;
        }
    } else {
        nameSource = (uint8_t *)tf->teamName;
    }
    StringCopy(nameSource, (uint8_t *)tg->teamName);
}

/* ========================================================================
 * Chunk 191 — SetupPlayers, InitAndPlayGame
 * ======================================================================== */

/* Externs for Chunk 191 */
extern uint16_t _careerTeamCoachOnly;
extern uint16_t _isCareer;
extern uint16_t _exitGameFlag;
extern uint16_t _twoPlayers;
extern uint16_t _gameCanceled;
extern uint16_t _maxSubstitutes;
extern uint16_t _minSubstitutes;
extern uint8_t  _gameName[];
extern uint8_t  _gameRound;
extern uint16_t _pitchTypeOrSeason;
extern uint16_t _gamePitchTypeOrSeason;
extern uint16_t _friendlySeason;
extern uint16_t _gameSeason;
extern uint16_t _friendlyPitchType;
extern uint16_t _gamePitchType;
extern uint16_t _team1GoalsFirstLeg;
extern uint16_t _team2GoalsFirstLeg;
extern int16_t  _extraTimeState;
extern int16_t  _penaltiesState;
extern uint16_t _secondLeg;
extern uint16_t _playing2ndGame;
extern uint16_t _isGameFriendly;
extern uint8_t  _aTraining[];
extern uint8_t  _aFriendly_1[];
extern uint8_t  _aShootingPractice[];   /* OpenSWOS: the drill's match name */
extern uint8_t  _aPenaltyShootout[];    /* OpenSWOS: the shootout's match name */

/* ShowPlayMatchMenu declared in play_match_menu.h */
/* InitializeInGameTeamsAndStartGame declared in game_init.h */

extern uint16_t ShouldSetupPlayers(void);
extern void InitPlayerRemapTableForTeam(TeamFileEx *team, uint8_t *remapTable);

/* SetupPlayers
 *
 * Shows play match menu for one or both human players before a match.
 * Handles career team remap, coach mode, and player/coach number assignment.
 *
 * Parameters:
 *   team1, team2      — TeamFileEx pointers (asm a1, a2)
 *   maxSubs           — max substitutes (asm d1)
 *   excludeFlags      — passed through to ShowPlayMatchMenu d2
 *   allowSquadChanges — enable squad changes in menu (asm d6)
 *   mode              — mode flag (asm d7); 0 = normal, non-zero = skip player assignment
 */
__attribute__((used, externally_visible))
void SetupPlayers(TeamFileEx *team1, TeamFileEx *team2, int16_t maxSubs,
                   int16_t excludeFlags, int16_t allowSquadChanges, int16_t mode) {
    uint16_t d0 = 0;

    _careerTeamCoachOnly = 0;

    /* A SAVED POSITION (src/challenge.c). Same shape as the three branches below -- the
     * replay rig's, a live netplay match's and the arcade's -- and for the same reason:
     * this function's tail opens ShowPlayMatchMenu, which waits for input, and these
     * paths have nobody to ask. A position is better off than the others, though: who
     * controlled which side, with which tactics, was decided when the position was
     * saved and travels in the file, so the answers are RESTORED rather than
     * re-derived. Outside the SDL guard below because a saved position loads on the
     * Amiga too, where this menu blocks exactly the same way. */
    if (Challenge_Armed()) {
        _isCareer = 0;
        _exitGameFlag = 0;
        Challenge_ApplySetup();     /* the control scalars this function would have set */
        (void)maxSubs; (void)excludeFlags; (void)allowSquadChanges; (void)mode; (void)d0;
        return;
    }

    {
        /* CHALLENGE GENERATOR (src/challenge_gen.c): the spec already decided who
         * controls which side, and a headless generator run has nobody to answer the
         * prompts. Same shape as the branches around it. */
        extern int32_t ChallengeGen_Armed(void);
        extern void    ChallengeGen_SetupPlayers(TeamFileEx *t1, TeamFileEx *t2);
        if (ChallengeGen_Armed()) {
            _isCareer = 0;
            _exitGameFlag = 0;
            ChallengeGen_SetupPlayers(team1, team2);
            (void)maxSubs; (void)excludeFlags; (void)allowSquadChanges; (void)mode; (void)d0;
            return;
        }
    }

#ifdef SWOS_SDL_BUILD
    /* A kit chosen on the UNIFORM screen is for one match: whatever was chosen before
     * the last one is forgotten before this one's team screens come up. */
    Uniform_Clear();
    {
        /* Recorded-game replay: the two ShowPlayMatchMenu prompts are
         * replaced by the control assignment the recording's header carries
         * (docs/TESTS.md 5b), mirroring the reference rig's menu callback. */
        extern int  ReplayTest_Armed(void);
        extern void ReplayTest_SetupPlayers(TeamFileEx *t1, TeamFileEx *t2);
        if (ReplayTest_Armed()) {
            _careerTeamCoachOnly = 0;
            ReplayTest_SetupPlayers(team1, team2);
            (void)maxSubs; (void)excludeFlags; (void)allowSquadChanges; (void)mode; (void)d0;
            return;
        }
    }
    {
        /* A LIVE netplay match: both teams are human, one of them on the other
         * machine, so there is nobody here to answer a prompt -- and a menu
         * that waits for input would never return, leaving GGPO's socket
         * filling up behind it. Measured: two peers sat at kick-off with 28 KB
         * queued and unread, because InitAndPlayGame had not reached the game
         * loop that drives the session at all.
         *
         * Same shape as the arcade branch below and the replay rig's above:
         * assign control directly instead of asking. Top team is player 1
         * (whoever created the game), bottom is player 2. */
        extern int32_t NetplayMatch_IsLive(void);
        if (NetplayMatch_IsLive()) {
            _isCareer = 0;
            _exitGameFlag = 0;
            _team1SelectedPlayerIndex = 0xFFFF;
            _team2SelectedPlayerIndex = 0xFFFF;
            _twoPlayers = 1;
            _pl1Coach = 0;
            _pl2Coach = 0;
            _topTeamPlayerNo    = 1;
            _topTeamCoachNo     = 0;
            _bottomTeamPlayerNo = 2;
            _bottomTeamCoachNo  = 0;
            _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
            _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
            (void)maxSubs; (void)excludeFlags; (void)allowSquadChanges; (void)mode; (void)d0;
            return;
        }
    }
    {
        extern int _gArcadeMode;
        if (_gArcadeMode) {
            /* Arcade: no pre-match squad/tactics menu — and the front-end menu
             * stack isn't initialised on this path anyway. Assign joystick control
             * directly and use each team's default formation/tactics: the same
             * outputs the normal SetupPlayers tail produces, with the
             * ShowPlayMatchMenu returns hard-wired (top team = joystick 1,
             * bottom = joystick 2 in 2P or the computer in 1P). */
            _isCareer = 0;
            _exitGameFlag = 0;
            _team1SelectedPlayerIndex = 0xFFFF;
            _team2SelectedPlayerIndex = 0xFFFF;
            _twoPlayers = (team1->team.teamControls != TEAM_COMPUTER &&
                           team2->team.teamControls != TEAM_COMPUTER) ? 1 : 0;
            _pl1Coach = 0;
            _pl2Coach = 0;
            _topTeamPlayerNo    = (team1->team.teamControls != TEAM_COMPUTER) ? 1 : 0;
            _topTeamCoachNo     = 0;
            _bottomTeamPlayerNo = (team2->team.teamControls != TEAM_COMPUTER) ? 2 : 0;
            _bottomTeamCoachNo  = 0;
            _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
            _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
            (void)maxSubs; (void)excludeFlags; (void)allowSquadChanges; (void)mode; (void)d0;
            return;
        }
    }
#endif

    /* Career team remap check */
    if (_isCareer == 0) {
        if (team1 == &_careerTeam || team2 == &_careerTeam) {
            if (ShouldSetupPlayers() == 0) {
                InitPlayerRemapTableForTeam(&_careerTeam, (uint8_t *)0);
            }
        }
    }

    _isCareer = 0;
    _exitGameFlag = 0;
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;

    if (team1->team.teamControls != TEAM_COMPUTER &&
        team2->team.teamControls != TEAM_COMPUTER) {
        _twoPlayers = 1;
    }

    /* Set pl1Coach: seq sets high byte to 0xFF → word = 0xFF00 */
    _pl1Coach = 0;
    if (team1->team.teamControls == TEAM_COACH)
        *(uint8_t *)&_pl1Coach = 0xFF;

    _topTeamPlayerNo = 0;
    _topTeamCoachNo = 0;

    /* Skip first menu only if team1 is computer and team2 is not.
     * If both are computer, still show the menu (computer vs computer viewing). */
    if (team1->team.teamControls == TEAM_COMPUTER &&
        team2->team.teamControls != TEAM_COMPUTER)
        goto setup_team2;

    /* Show play match menu for first player */
    d0 = ShowPlayMatchMenu(3, maxSubs, excludeFlags, 1, allowSquadChanges,
                              team1, team2);

    if (_exitGameFlag != 0) goto cancelled;

    if (_careerTeamCoachOnly != 0) {
        _pl1Coach = 0;
        _topTeamPlayerNo = 0;
        _topTeamCoachNo = 0;
    }

    if (team1->team.teamControls != TEAM_COMPUTER && mode == 0) {
        if (team1->team.teamControls == TEAM_COACH) {
            _topTeamCoachNo = d0;
        } else {
            _topTeamPlayerNo = d0;
        }
    }

setup_team2:
    /* Set pl2Coach: seq pattern same as pl1Coach */
    _pl2Coach = 0;
    if (team2->team.teamControls == TEAM_COACH)
        *(uint8_t *)&_pl2Coach = 0xFF;

    _bottomTeamPlayerNo = 0;
    _bottomTeamCoachNo = 0;

    if (team2->team.teamControls == TEAM_COMPUTER)
        goto set_tactics;

    if (_careerTeamCoachOnly != 0)
        goto skip_second_menu;

    /* Calculate exclude flags for second player:
     * d0 = (topTeamPlayerNo | topTeamCoachNo) ^ 3 */
    d0 = (_topTeamPlayerNo | _topTeamCoachNo) ^ 3;

    /* Show second player menu with teams swapped (team2 as first, team1 as second) */
    d0 = ShowPlayMatchMenu(d0, maxSubs, excludeFlags, 0, allowSquadChanges,
                              team2, team1);

    if (_exitGameFlag != 0) goto cancelled;

    if (mode != 0)
        goto set_tactics_swapped;

skip_second_menu:
    if (team2->team.teamControls == TEAM_COACH) {
        _bottomTeamCoachNo = d0;
    } else {
        _bottomTeamPlayerNo = d0;
    }

set_tactics:
    _pl1Tactics = (int16_t)(int8_t)team1->team.tactics;
    _pl2Tactics = (int16_t)(int8_t)team2->team.tactics;
    return;

set_tactics_swapped:
    /* When mode != 0, teams stay swapped (exg a1,a2 not undone) */
    _pl1Tactics = (int16_t)(int8_t)team2->team.tactics;
    _pl2Tactics = (int16_t)(int8_t)team1->team.tactics;
    return;

cancelled:
    _gameCanceled = 1;
    _isCareer = 0;
    if (team1 == &_careerTeam || team2 == &_careerTeam) {
        _isCareer = 1;
    }
}

/* InitAndPlayGame
 *
 * Entry point for friendly/training matches. Calls SetupPlayers for player
 * configuration, then sets up game parameters and calls
 * InitializeInGameTeamsAndStartGame.
 *
 * Parameters:
 *   team1, team2 — TeamFileEx pointers (asm a1, a2)
 */
__attribute__((used, externally_visible))
void InitAndPlayGame(TeamFileEx *team1, TeamFileEx *team2) {
    /* SetupPlayers: mode=0, maxSubs=_maxSubstitutes, excludeFlags=1, allowSquadChanges=1 */
    SetupPlayers(team1, team2, (int16_t)_maxSubstitutes, 1, 1, 0);

    if (_exitGameFlag != 0)
        goto done;

    /* Set game name */
    if (_trainingGame != 0) {
        StringCopy(_aTraining, _gameName);
    } else {
#ifdef SWOS_SDL_BUILD
        extern int _gArcadeMode;
        extern uint8_t _aArcadeMatchName[];   /* "GOLDEN GOAL" (arcade.c) */
        if (_gArcadeMode)
            StringCopy(_aArcadeMatchName, _gameName);
        else
#endif
        /* SHOOTING PRACTICE and PENALTY SHOOTOUT are friendlies in every other
         * respect, but the name is what the scoreboard and a saved highlight
         * carry, so it should say which it was (src/shooting_practice.c,
         * src/penalty_shootout.c). */
        StringCopy(g_shootingPracticeMode ? _aShootingPractice
                 : g_penaltyShootoutMode  ? _aPenaltyShootout
                                          : _aFriendly_1,
                   _gameName);
    }
    _gameRound = 0;

    /* Set game parameters */
    _gamePitchTypeOrSeason = _pitchTypeOrSeason;
    _gameSeason = _friendlySeason;
    _gamePitchType = _friendlyPitchType;
    _team1GoalsFirstLeg = 0;
    _team2GoalsFirstLeg = 0;
    _extraTimeState = 0;
    _penaltiesState = 0;
    _secondLeg = 0;
    _playing2ndGame = 0;
    _isGameFriendly = 1;
#ifdef SWOS_SDL_BUILD
    {
        /* An online match's cup rules (the lobby's), put back after the clear. */
        extern void NetplaySetup_TakeTieRules(void);
        NetplaySetup_TakeTieRules();
    }
#endif

    /* d0=1, d1=minSubs, d2=maxSubs, d3=0, d7=-1, a1=team1, a2=team2 */
    InitializeInGameTeamsAndStartGame(1, (int16_t)_minSubstitutes,
        (int16_t)_maxSubstitutes, 0, -1, &team1->team, &team2->team);

done:
    _isCareer = 0;
}

/* ========================================================================
 * Chunk 214 — InitializeInGameTeamsAndStartGame
 * ======================================================================== */

/* Additional externs for Chunk 214 */
extern uint16_t _hilStarted;
extern uint16_t _plgD0Param;
extern uint16_t _maxSubsPerTeam;
extern TeamFile *_gameTeam1;
extern TeamFile *_gameTeam2;
extern uint16_t _teamsLoaded;
extern uint16_t _poolplyrLoaded;
extern uint16_t _team1Computer;
extern uint16_t _team2Computer;
extern int16_t  _team1AppPercent;
extern int16_t  _team2AppPercent;
extern uint16_t _team1NumAllowedInjuries;
extern uint16_t _team2NumAllowedInjuries;
extern uint16_t _menuMusic;
extern uint16_t _deadVarAlways0;
extern uint16_t _gameLength;
extern uint16_t _gameLengthInGame;
extern volatile uint16_t _currentGameTick;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */
extern uint8_t  _stadiumMenu[];

/* C function declarations for called routines */
extern void SetTeamSecondaryColors(uint16_t shirtType, uint8_t stripesColor,
    uint8_t basicColor, uint8_t shortsColor, uint8_t socksColor, TeamGame *team);
extern void SetInGameTeamsPrimaryColors(TeamFile *team1File, TeamFile *team2File);
extern uint16_t ComputeOldPlayerSkillForViewResult(TeamFileEx *team, uint16_t goalsCount);
extern uint16_t GetAveragePlayerPriceInSelectedTeams(void);
extern void InitInGameTeamStructure(TeamFileEx *teamFileBase, TeamGame *tg, int16_t *avgPricePct);
extern uint32_t GetNumberOfAvailablePlayers(uint8_t *team, int16_t excludeCardsFlag, uint16_t numPl);
extern void StopMusic(void);
extern void ShowMenu(uint8_t *packedMenu);
extern void ShowMatchSequence(void);
extern void InitMenuMusic(void);
extern void TryPlayMusic(void);
extern void PrepareMenuForFadeIn(void);
extern void InitInGameTeams_PlayerControlled(TeamFileEx *team1, TeamFileEx *team2, uint16_t mode);
extern void AccumulateMatchGoalsIntoTeamFile(TeamGame *tg, TeamFileEx *teamFileBase, uint16_t statOffset);
extern void UpdatePlayerInjuries(TeamGame *tg, TeamFileEx *teamFileBase);
extern void ApplyPlayerStatusAfterSim(TeamGame *tg, TeamFileEx *teamFileBase);
extern void UpdateCareerPlayerStatsAfterMatch(TeamGame *tg, TeamFileEx *teamFileBase);

/* InitializeInGameTeamsAndStartGame
 *
 * Sets up teams for a match, displays stadium menu, plays match,
 * then handles post-match statistics and injuries.
 *
 * Parameters (from asm register convention):
 *   randomizePitch (d0) — pitch randomization param
 *   minSubs (d1)        — minimum substitutes
 *   maxSubs (d2)        — maximum substitutes
 *   paramD3 (d3)        — passed to plgD3Param
 *   paramD7 (d7)        — flags; -1 = skip post-match processing
 *   team1 (a1)          — first team (TeamFile)
 *   team2 (a2)          — second team (TeamFile)
 */
__attribute__((used, externally_visible))
void InitializeInGameTeamsAndStartGame(int16_t randomizePitch, int16_t minSubs,
    int16_t maxSubs, int16_t paramD3, int16_t paramD7,
    TeamFile *team1, TeamFile *team2) {
    uint16_t d0;
    int16_t disabledCount;
    int16_t allowedInjuries;
    uint32_t avail;

    _hilStarted = 0;
    /* The Rand2 save stack starts empty for every match.
     *
     * SaveRand2State / RestoreRand2State push and pop 3 bytes of secondary-generator
     * state through _savedRandStatePtr, and every caller is balanced, so the pointer is
     * normally back at the base by now -- but "normally" is not a guarantee a match
     * should rest on, and it is the one piece of RNG state that InitDeterministicMode
     * resets which netplay's setup does NOT carry across (src/netplay_setup.c sends all
     * six generator bytes and what they are re-seeded from; there is nowhere in that blob
     * for a stack depth). Two peers that came to the lobby through different menus would
     * start a match with the pointer at different depths, and the deeper one would panic
     * (0x9793) rather than desync -- but neither belongs in a match, and this costs a
     * store. Nothing saves state across a kick-off: the callers are the career screens
     * and ApplyTeamTactics, each balanced inside one function. */
    { extern uint8_t _savedRandState[]; extern uint8_t *_savedRandStatePtr;
      _savedRandStatePtr = _savedRandState; }
    _plgD0Param = (uint16_t)randomizePitch;
    _maxSubsPerTeam = (uint16_t)minSubs;
    _gameMaxSubstitutes = (uint16_t)maxSubs;
    _plgD3Param = (uint16_t)paramD3;
    _gameTeam1 = team1;
    _gameTeam2 = team2;

    /* Compute old player skill for view result (for both teams) */
    ComputeOldPlayerSkillForViewResult((TeamFileEx *)team1, (uint16_t)maxSubs);
    ComputeOldPlayerSkillForViewResult((TeamFileEx *)team2, (uint16_t)maxSubs);

    _teamsLoaded = 0;
    _poolplyrLoaded = 0;

    /* Set computer flags:
     * clr.w + seq on low byte → 0x00FF if computer, 0x0000 otherwise */
    _team1Computer = 0;
    _team2Computer = 0;
    if (team1->teamControls == TEAM_COMPUTER)
        _team1Computer = 0x00FF;
    if (team2->teamControls == TEAM_COMPUTER)
        _team2Computer = 0x00FF;

    /* SHOOTING PRACTICE / PENALTY SHOOTOUT: latch the modes for this match now
     * that both sides' controls are known, so nothing during the match depends
     * on a menu global still being set (src/shooting_practice.c,
     * src/penalty_shootout.c). */
    ShootingPractice_BeginMatch();
    PenaltyShootout_BeginMatch();

    /* Set secondary colors for both teams */
#ifdef SWOS_SDL_BUILD
    {
        /* The kits come from the teams -- or, for a side that chose one on the UNIFORM
         * screen, from that choice, written over both kits of a COPY of the team's
         * header (src/uniform_menu.c). Nothing after this reads a kit from the team
         * file: the match, its replay and a rollback all work from the in-game teams. */
        static TeamFile kit1, kit2;
        int32_t chosen;
        extern int32_t NetplayMatch_Enabled(void);
        extern int32_t NetplayMatch_IsLive(void);
        extern void SetInGameTeamsFirstKits(TeamFile *team1File, TeamFile *team2File);
        /* A netplay match (live, or one replayed from its recording) is dressed from
         * the teams alone: a choice made online is already IN the team both machines
         * have (online_menus.c lineup_end), and one left over on this machine would
         * dress the side differently here than there. */
        if (NetplayMatch_Enabled())
            Uniform_Clear();
        chosen = Uniform_Kits(team1, team2, &kit1, &kit2);
        SetTeamSecondaryColors((uint16_t)kit1.prShirtType, kit1.prStripesColor,
            kit1.prBasicColor, kit1.prShortsColor, kit1.prSocksColor, &_topTeamInGame);
        SetTeamSecondaryColors((uint16_t)kit2.prShirtType, kit2.prStripesColor,
            kit2.prBasicColor, kit2.prShortsColor, kit2.prSocksColor, &_bottomTeamInGame);
        /* Chosen kits are worn as chosen, as in SWOS 2020 (ReportBot 0028): the game's
         * rule for kits that are too alike is asked only when nobody chose. Online it is
         * never asked -- each player's team arrives with the kit they settled on as its
         * first, and both machines dress the match the same way from that alone. */
        if (chosen || NetplayMatch_IsLive())
            SetInGameTeamsFirstKits(&kit1, &kit2);
        else
            SetInGameTeamsPrimaryColors(&kit1, &kit2);
        /* What each side kicks off in, for SWOS.log: shirt type, then the palette
         * entries of the shirt, its second colour, the shorts and the socks. */
        fprintf(stderr, "[match] kits: %.16s type %d colours %d/%d/%d/%d, %.16s type %d colours %d/%d/%d/%d\n",
                (const char *)team1->teamName, _topTeamInGame.prShirtType, _topTeamInGame.prShirtCol,
                _topTeamInGame.prStripesCol, _topTeamInGame.prShortsCol, _topTeamInGame.prSocksCol,
                (const char *)team2->teamName, _bottomTeamInGame.prShirtType, _bottomTeamInGame.prShirtCol,
                _bottomTeamInGame.prStripesCol, _bottomTeamInGame.prShortsCol, _bottomTeamInGame.prSocksCol);
    }
#else
    SetTeamSecondaryColors((uint16_t)team1->prShirtType, team1->prStripesColor,
        team1->prBasicColor, team1->prShortsColor, team1->prSocksColor, &_topTeamInGame);
    SetTeamSecondaryColors((uint16_t)team2->prShirtType, team2->prStripesColor,
        team2->prBasicColor, team2->prShortsColor, team2->prSocksColor, &_bottomTeamInGame);

    /* Set primary colors */
    SetInGameTeamsPrimaryColors(team1, team2);
#endif

    /* If not career and all player teams are equal, compute average price */
    if (_gameType != GAME_TYPE_CAREER && _allPlayerTeamsEqual != 0) {
        _avgSelectedTeamsPlayerPrice = GetAveragePlayerPriceInSelectedTeams();
    }

    /* Initialize in-game team structures */
    InitInGameTeamStructure((TeamFileEx *)team1, &_topTeamInGame, &_team1AppPercent);
    InitInGameTeamStructure((TeamFileEx *)team2, &_bottomTeamInGame, &_team2AppPercent);
#ifdef SWOS_SDL_BUILD
    /* The eleven each side kicks off with, in line-up order, for SWOS.log: a match
     * report then says who was where (the roll for a contested ball goes by line-up
     * slot, docs/BALL_CONTEST.md), and a line-up that did not reach the match shows. */
    {
        const TeamGame *tg[2] = { &_topTeamInGame, &_bottomTeamInGame };
        int32_t t, i;
        for (t = 0; t < 2; t++) {
            fprintf(stderr, "[match] line-up %d, %s:", (int)t + 1, (const char *)tg[t]->teamName);
            for (i = 0; i < 11; i++)
                fprintf(stderr, "%s %.15s", i ? "," : "", (const char *)tg[t]->players[i].shortName);
            fprintf(stderr, "\n");
        }
    }
#endif

    /* Calculate allowed injuries for each team */
    if (_gameType == GAME_TYPE_CAREER) {
        /* Career mode: allow up to 4 injuries */
        avail = GetNumberOfAvailablePlayers((uint8_t *)team1, -1, 0);
        disabledCount = (int16_t)(avail >> 16); /* d6 = disabled */
        allowedInjuries = 4 - disabledCount;
        if (allowedInjuries < 0) allowedInjuries = 0;
        _team1NumAllowedInjuries = (uint16_t)allowedInjuries;

        avail = GetNumberOfAvailablePlayers((uint8_t *)team2, -1, 0);
        disabledCount = (int16_t)(avail >> 16);
        allowedInjuries = 4 - disabledCount;
        if (allowedInjuries < 0) allowedInjuries = 0;
        _team2NumAllowedInjuries = (uint16_t)allowedInjuries;
    } else {
        /* Non-career: allow up to 2 injuries */
        avail = GetNumberOfAvailablePlayers((uint8_t *)team1, -1, 0);
        disabledCount = (int16_t)(avail >> 16);
        allowedInjuries = 2 - disabledCount;
        if (allowedInjuries < 0) allowedInjuries = 0;
        _team1NumAllowedInjuries = (uint16_t)allowedInjuries;

        avail = GetNumberOfAvailablePlayers((uint8_t *)team2, -1, 0);
        disabledCount = (int16_t)(avail >> 16);
        allowedInjuries = 2 - disabledCount;
        if (allowedInjuries < 0) allowedInjuries = 0;
        _team2NumAllowedInjuries = (uint16_t)allowedInjuries;
    }

    /* Override for friendly games */
    if (_isGameFriendly != 0) {
        _team1NumAllowedInjuries = 4;
        _team2NumAllowedInjuries = 4;
    }

    /* Stop music if playing */
    if (_menuMusic != 0) {
        StopMusic();
    }

    /* Show stadium menu */
    ShowMenu(_stadiumMenu);

    /* Wait 150 frames (~3 seconds at 50Hz). Original was a busy spin on
     * currentGameTick; on SDL that starves the timer thread on WSL2 and
     * the wait can hang indefinitely (caught by user with SIGINT). Use
     * WaitForVSync so the main thread yields each frame and pumps
     * events / audio. */
    {
        int16_t i;
        for (i = 0x95; i >= 0; i--) {
            WaitForVSync();
        }
    }

    _deadVarAlways0 = 0;
    _gameLengthInGame = _gameLength;

    /* For non-friendly, non-season games, clear game length */
    if (_isGameFriendly == 0 && _gameType != 1) {
        _gameLengthInGame = 0;
    }

    /* Play the match */
    ShowMatchSequence();

    if (_gameCanceled != 0)
        goto post_match;

    if (_trainingGame != 0) {
        /* Training game: set flag and update injuries only */
        team1->andWith0xFE |= 1; /* bset #0,$17(a0) */
        team2->andWith0xFE |= 1;

        UpdatePlayerInjuries(&_topTeamInGame, (TeamFileEx *)team1);
        UpdatePlayerInjuries(&_bottomTeamInGame, (TeamFileEx *)team2);
        goto post_match;
    }

    /* Non-training: full post-match processing */
    InitInGameTeams_PlayerControlled((TeamFileEx *)team1, (TeamFileEx *)team2, (uint16_t)_plgD3Param);

    if (paramD7 < 0)
        goto post_match;

    /* Process team 1 */
    AccumulateMatchGoalsIntoTeamFile(&_topTeamInGame, (TeamFileEx *)team1, (uint16_t)paramD7);
    UpdatePlayerInjuries(&_topTeamInGame, (TeamFileEx *)team1);
    ApplyPlayerStatusAfterSim(&_topTeamInGame, (TeamFileEx *)team1);
    UpdateCareerPlayerStatsAfterMatch(&_topTeamInGame, (TeamFileEx *)team1);

    /* Process team 2 */
    AccumulateMatchGoalsIntoTeamFile(&_bottomTeamInGame, (TeamFileEx *)team2, (uint16_t)paramD7);
    UpdatePlayerInjuries(&_bottomTeamInGame, (TeamFileEx *)team2);
    ApplyPlayerStatusAfterSim(&_bottomTeamInGame, (TeamFileEx *)team2);
    UpdateCareerPlayerStatsAfterMatch(&_bottomTeamInGame, (TeamFileEx *)team2);

post_match:
    {
        int doMenuMusic = 1;
#ifdef SWOS_SDL_BUILD
        /* Arcade restarts its own (HERO) attract music and returns to the attract
         * loop, so skip loading + playing menu music here — a redundant reconfigure
         * of the shared RJP1 player right before HERO takes it over. */
        extern int _gArcadeMode;
        if (_gArcadeMode) doMenuMusic = 0;
#endif
        if (doMenuMusic) {
            InitMenuMusic();
            if (_menuMusic != 0) {
                TryPlayMusic();
            }
        }
    }

    PrepareMenuForFadeIn();
    /* gameCanceled is tested by caller via return/tst.w */
}
