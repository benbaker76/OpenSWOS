/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "teams.h"
#include "career_teams.h"
#include "comp_identity.h"
#include "swos_platform.h"   /* HLOG — force-included by the SDL Makefile, not the
                               Amiga one, so include it explicitly */
#include "endian_helpers.h"  /* BE_READ_U16 / PACK_BE_U16 */
#include "file_io.h"
#include "deterministic.h"
#include "menus.h"
#include "string_utils.h"
#include "input_text.h"
#include "panic.h"
#include "diy_competition.h"
#include "career_status.h"
#include "play_match_menu.h"
#include "career_init.h"
#include "shooting_practice.h"  /* SHOOTING PRACTICE gates the team selection below */
#include "champions36.h"        /* the 36-team european preset substitution */
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>            /* getenv — SWOS_DATA_JSON team-load wire-in */
#endif

/* ---- Constants ---- */
#define GAME_TYPE_CAREER            4
#define ORIGINAL_CUSTOM_TEAMS   2
#define EDITED_CUSTOM_TEAMS     3
#define CN_EUROPE           80
#define CN_AFRICA           81
#define CN_SOUTH_AMERICA    82
#define CN_NORTH_AMERICA    83
#define CN_ASIA             84
#define CN_OCEANIA          85

/* ---- External functions ---- */
extern void SetExitMenuFlag(void);

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _teamsLoaded;
extern uint8_t _lastLoadedExtNumber;
extern uint8_t _teamFileBuffer[];
extern uint8_t _teamFilenameBuffer[];
extern uint16_t _teamFileTeamCount;
extern uint32_t _teamFileDataSize;
extern uint16_t _gameType;
extern uint16_t _skipLeagueNumbers;
extern uint8_t _diyLeagueSeasonStateTable[];
extern volatile uint16_t _panicReasonCode;
extern uint16_t _isNationalTeam;
extern uint16_t _numSelectedTeams;

/* Data labels (mutable strings and tables in SWOS2.s) */
extern uint8_t _aDataTeamNnn[];      /* "data/team.nnn" */
extern uint8_t _aCustomsEdt[];       /* "CUSTOMS.EDT" */
extern uint8_t _aEditedCustomTe[];   /* "EDITED CUSTOM TEAMS FILE NOT FOUND" */
extern uint16_t _teamsCountryNumbers[];
extern uint8_t _avgTeamValues[];
extern uint16_t _avgTeamValueBases[];

/* Globals for GetPlayerRandomNum / GetDisplayedPlayerValue */
extern uint16_t _currentSeasonShowing;
extern uint8_t _newRandomSeedCopy;
extern uint8_t _kRandomTable[];
extern uint8_t _currentTeam[];
extern uint8_t _playerPositions[];
extern TeamFileEx _careerTeam;
extern uint8_t _careerNumPlayers[];
extern uint8_t _viewRandomPhase;
extern uint8_t displayedPlayerValueRandomDeltaTable[];

/* Globals for GetPlayer / GetPlayerAtIndex / GetPlayerSlotIndexByPlayerNumber */
extern uint16_t _currentTeamNumber;
extern uint16_t _selTeamNumber;
extern uint8_t _currentMatchPlayers[];
extern uint16_t _cGetPlayerAtIndexFileIdx;

/* Globals for national selector functions */
extern uint16_t _careerJobOfferMode;
extern uint32_t _nationalSelectorSeasonMaskA;
extern uint32_t _nationalSelectorSeasonMaskB;
extern uint8_t _nationalTeams[];
extern uint16_t _numNationalTeams;

/* Globals for team selection menu (Chunks 107-109) */
extern uint16_t _changeTeams;
extern uint16_t _chooseTeamsResult;
extern uint16_t _viewSelectedTeams;
extern uint16_t _abortSelectTeams;
extern uint16_t _exitSelectTeams;
extern int16_t  _topOfStackMark;

/* Globals for SelectTeamsFinalMenu */
extern uint8_t _customsLeague[];
extern void *_competitionsTable[];
extern void *_teamNames[];
extern void *_leaguesPtrTable[];
extern uint16_t _leagueNumbersTable[];
extern void *_countryNames[];
extern uint16_t _numLeaguesInCountry;
extern uint16_t _chooseTeamsNumTeams;
extern int16_t  _selectedTeamIndex;
extern int16_t  _selectedLeagueIndex;
extern uint16_t _showCupsAndOther;
extern uint16_t _playSeason;
extern uint16_t _loadTeam;
extern uint16_t _stayInSelectTeams;
extern uint16_t _exitFlag;
extern uint16_t _isContestContinentalCup;
extern uint16_t _isCountryNumber;
extern uint16_t _gotCountryName;
extern uint16_t _numberOfLeagues;
extern uintptr_t _selectedContest;
extern uint16_t _selectTeamsNoLimit;
extern uintptr_t _chooseTeamsHeader1stPart;
extern uintptr_t _selectedTeamText;
extern uintptr_t _gameTypeStr;
extern uintptr_t _playWhatStr;
extern uintptr_t _entryText2;
extern uint8_t  _someTeamFlags;
extern uint16_t _isNationalSelector;
extern uint16_t _chooseCompetitionOnly;
extern uint16_t _countryNumber;
extern uint16_t _leagueNumber;
extern uint16_t _contestLeagueNumber;
extern const char *_divisionStringTable[];
extern uint8_t  _selectTeamsMenu[];
extern uint8_t  _chooseTeamsFriendlyMenu[];
extern uint8_t  _chooseTeamsFriendlyMenu2[];
extern uint8_t  _europeanChampionships[];
extern uint8_t  _worldCup[];
extern uint8_t  _africanNationsCup[];
extern uint8_t  _copaAmerica[];
extern Competition *Comp_ById(uint8_t compId);  /* comp record by compId (competition_data.h) */
extern uint8_t  _northAmericanCup[];
extern uint8_t  _asianCup[];
extern uint8_t  _oceaniaCup[];
extern uint8_t  _northAmericaWorldCupQualification[];
extern uint8_t  _southAmericaWorldCupQualification[];
extern uint8_t  _africaWorldCupQualification[];
extern uint8_t  _asiaWorldCupQualification[];
extern uint8_t  _oceaniaWorldCupQualification[];
extern uint8_t  _aEditedCustomTe_0[]; /* "EDITED CUSTOM TEAMS" */
extern uint8_t  _aPlaySeason[];       /* "PLAY SEASON" */
extern uint8_t  _aPlayTournament[];   /* "PLAY TOURNAMENT" */
extern uint8_t  _aPlayCup[];          /* "PLAY CUP" */
extern uint8_t  _aPlayLeague[];       /* "PLAY LEAGUE" */
extern const char _aCup_0[];          /* "CUP" */
extern const char _aLeague_1[];       /* "LEAGUE" */
extern uint8_t  _countriesStack[];

/* Globals for team navigation stack (Chunk 109) */
extern void *_countriesTable[];
extern uint8_t *_curCountryStr;
extern uint8_t *_countryStackPtr;
extern uint16_t _teamDepthLevel;
extern uint16_t _selectedTeamsStack[];
extern uint16_t _lastSelectedTeam;
extern uint16_t _prevSelectedTeamEntry;
extern uint16_t _isCountryNumber;
extern uint16_t _gotCountryName;
extern uint16_t _numberOfLeagues;
extern uint16_t _isContestContinentalCup;
extern uint32_t _continentalCups[];
extern uintptr_t _selectedContest;
extern uint16_t _teamsType;
extern uint32_t _nationalTeamCompetitionsAndQualifications[];

/* ================================================================
 * SetTeamGlobalNumbers  (SWOS2.s line 112842)
 *
 * For each team in the buffer, computes the global team number
 * from the country number and team-within-country number using
 * the teamsCountryNumbers lookup table.
 *
 * Asm register convention:
 *   in:  d0 = numTeams, a0 = pointer to first team entry
 * ================================================================ */
__attribute__((used, externally_visible))
void SetTeamGlobalNumbers(uint16_t numTeams, uint8_t *firstTeam)
{
    uint16_t i;
    TeamFileEx *team;

    if (numTeams == 0)
        return;

    team = (TeamFileEx *)firstTeam;
    for (i = numTeams; i > 0; i--) {
        uint16_t countryNum = team->team.countryNumber & 0xFF;
        uint16_t localNum;
        uint16_t globalNum;

        countryNum = countryNum << 1;   /* word index */
        globalNum = *((uint16_t *)((uint8_t *)_teamsCountryNumbers + countryNum));

        localNum = team->team.teamOrdinal & 0xFF;
        globalNum += localNum;

        team->team.globalTeamNumber = (int16_t)globalNum;

        team++;
    }
}

/* ================================================================
 * setLeagueNumbers  (SWOS2.s line 121389)
 *
 * For each team in teamFileBuffer, reads the global team number
 * and uses diyLeagueSeasonStateTable to add a league offset
 * to the team's league field.
 * ================================================================ */
static void setLeagueNumbers(void)
{
    uint8_t *teamBuf = _teamFileBuffer;
    uint16_t count = *((uint16_t *)teamBuf);
    TeamFileEx *team;
    uint16_t i;

    if (count == 0)
        return;

    team = (TeamFileEx *)(teamBuf + 2);
    for (i = count; i > 0; i--) {
        uint16_t globalNum = (uint16_t)team->team.globalTeamNumber;
        uint8_t leagueAdj = _diyLeagueSeasonStateTable[globalNum];
        team->team.league += leagueAdj;
        team++;
    }
}

/* ================================================================
 * setLoadedTeamsGlobalNumbers  (SWOS2.s line 121381)
 *
 * Reads team count from teamFileBuffer header and calls
 * SetTeamGlobalNumbers on the first team entry (offset +2).
 * ================================================================ */
static void setLoadedTeamsGlobalNumbers(void)
{
    uint16_t count = *((uint16_t *)_teamFileBuffer);
    SetTeamGlobalNumbers(count, _teamFileBuffer + 2);
}

/* ================================================================
 * LoadTeamFile  (SWOS2.s line 121301)
 *
 * Loads a team file given an extension number.
 * - extNumber == 100 → loads "CUSTOMS.EDT"
 * - otherwise → constructs "data/team.NNN" filename
 *
 * If already loaded with same extNumber, returns immediately.
 * On successful load, sets teamsCountryNumbers and optionally
 * league numbers (for career mode).
 *
 * Asm register convention:
 *   in:  d0 = extNumber (uint8_t effective)
 *   out: d0 = 0 (success) or 1 (error), Z flag reflects d0
 * ================================================================ */
#ifdef SWOS_SDL_BUILD
#include "endian_helpers.h"
/* Team files are stored big-endian (Amiga m68k native). On a little-endian
 * host, every multi-byte field would otherwise read byte-swapped. Within a
 * 684-byte team record only two uint16 fields exist:
 *   teamFileBuffer[0..1]              = team count (file header)
 *   teamFileBuffer[2 + i*684 + 2..3]  = TeamFile.globalTeamNumber per record
 * Everything else (TeamFile name fields, PlayerFile attributes) is
 * single-byte. Swap once after load so subsequent native reads work.
 *
 * Note: (countryNumber, teamOrdinal) at offsets 0/1 of each record are
 * two SEPARATE single-byte fields. Callers that want the 16-bit packed
 * team id must use PACK_BE_U16(), not a cast — see endian_helpers.h. */
/* BE → host-native (called after load). */
void byteswap_loaded_team_file(void);
void byteswap_loaded_team_file(void)
{
    uint8_t *buf = _teamFileBuffer;
    uint16_t count = BE_READ_U16(buf);
    *(uint16_t *)buf = count;                      /* store in host order */
    TeamFileEx *rec = (TeamFileEx *)(buf + 2);
    for (uint16_t i = 0; i < count; i++) {
        rec->team.globalTeamNumber =
            (int16_t)BE_READ_U16(&rec->team.globalTeamNumber);
        rec++;
    }
}

/* host-native → BE (called by edit_tactics.c before WriteFile(aCustomsEdt))
 * Naively calling byteswap_loaded_team_file twice does NOT undo the load
 * swap — the function reads its loop count via BE_READ_U16(buf), so on
 * host-native input it sees the byte-swapped count (e.g. 0x3000 / 12288
 * teams) and iterates ~8 MB past the buffer end. Use a dedicated function
 * that reads count host-native, swaps each per-team globalTeamNumber to BE,
 * then swaps the count itself. */
void byteswap_team_file_to_disk(void);
void byteswap_team_file_to_disk(void)
{
    uint8_t *buf = _teamFileBuffer;
    uint16_t count = *(uint16_t *)buf;             /* host-native read */
    TeamFileEx *rec = (TeamFileEx *)(buf + 2);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t gtn = (uint16_t)rec->team.globalTeamNumber;
        BE_WRITE_U16(&rec->team.globalTeamNumber, gtn);
        rec++;
    }
    BE_WRITE_U16(buf, count);                       /* swap count last */
}
#endif

__attribute__((used, externally_visible))
int16_t LoadTeamFile(uint16_t extNumber)
{
    uint8_t ext8 = (uint8_t)(extNumber & 0xFF);
    int16_t result;
    uint32_t fileSize;

    /* Check if already loaded with same extension */
    if (_teamsLoaded != 0) {
        if (_lastLoadedExtNumber == ext8)
            return 0;   /* already loaded */
    }

    _teamsLoaded = 1;

    if (ext8 == 100) {
        /* Custom teams file */
        _lastLoadedExtNumber = ext8;
        result = LoadFileEx(_aCustomsEdt, _teamFileBuffer, &fileSize);
        if (result != 0) {
            /* Custom file load failure */
            if ((uint16_t)result == 0xCD) {
                ShowErrorMenu(_aEditedCustomTe);
            } else {
                ShowFsErrorByCode((uint16_t)result);
            }
            _teamsLoaded = 0;
            return 1;
        }
        if (fileSize > 64000u) {
            _panicReasonCode = 0x1939;
            PanicHaltLoop();
        }
#ifdef SWOS_SDL_BUILD
        byteswap_loaded_team_file();
#endif
        setLoadedTeamsGlobalNumbers();
    } else {
        _lastLoadedExtNumber = ext8;

        int json_loaded = 0;
#ifdef SWOS_SDL_BUILD
        /* SWOS_DATA_JSON: load this country's teams from data_json/ instead of the
         * binary TEAM.NNN. Falls back to the binary file on any miss (country not
         * in the corpus, file missing, parse error). The JSON path writes
         * teamFileBuffer host-native, so it SKIPS the BE byteswap. */
        /* SWOS_DATA_JSON default-ON; SWOS_DATA_JSON=0 disables. */
        const char *djv = getenv("SWOS_DATA_JSON");
        if (!(djv && djv[0] == '0' && djv[1] == '\0')) {
            extern int DataJson_LoadTeamFileForCountry(int, uint8_t *, int);
            /* teamFileBuffer is 0xFA0E bytes → (0xFA0E-2)/sizeof(TeamFileEx) ≈ 93 */
            if (DataJson_LoadTeamFileForCountry((int)ext8, _teamFileBuffer, 93) > 0)
                json_loaded = 1;
        }
#endif

        if (!json_loaded) {
            /* Normal team file: construct "data/team.NNN" */
            uint8_t hundreds, tens, ones;
            uint16_t val;

            val = (uint16_t)ext8;
            hundreds = (uint8_t)(val / 100);
            val = val % 100;
            tens = (uint8_t)(val / 10);
            ones = (uint8_t)(val % 10);

            _aDataTeamNnn[0xA] = hundreds + '0';
            _aDataTeamNnn[0xB] = tens + '0';
            _aDataTeamNnn[0xC] = ones + '0';

            StringCopy(_aDataTeamNnn, _teamFilenameBuffer);

            result = LoadFileEx(_teamFilenameBuffer, _teamFileBuffer, &fileSize);
            if (result != 0) {
                /* Non-custom file load failure: panic and halt */
                _panicReasonCode = 0x8008;
                PanicHaltLoop();
                /* never returns */
            }
            if (fileSize > 64000u) {
                _panicReasonCode = 0x1939;
                PanicHaltLoop();
            }
#ifdef SWOS_SDL_BUILD
            byteswap_loaded_team_file();
#endif
        }

        /* Shared post-load processing (binary OR json source). */
        setLoadedTeamsGlobalNumbers();

        /* In career mode, also set league numbers */
        if (_gameType == GAME_TYPE_CAREER && _skipLeagueNumbers == 0) {
            setLeagueNumbers();
        }
    }

    /* Update team count and data size */
    {
        uint16_t count = *((uint16_t *)_teamFileBuffer);
        _teamFileTeamCount = count;
        _teamFileDataSize = (uint32_t)count * sizeof(TeamFileEx) + 2;
    }

    return 0;
}

/* ================================================================
 * CheckIsTeamNational  (SWOS2.s line 29246)
 *
 * Sets isNationalTeam to 1 if the first selected team's continent
 * byte is between CN_EUROPE (80) and CN_OCEANIA (85) inclusive,
 * i.e. the team is a national team. Otherwise sets it to 0.
 * ================================================================ */
__attribute__((used, externally_visible))
void CheckIsTeamNational(void)
{
    _isNationalTeam = 0;
    if (_numSelectedTeams == 0)
        return;
    if (_selTeamsPtr < CN_EUROPE)
        return;
    if (_selTeamsPtr > CN_OCEANIA)
        return;
    _isNationalTeam = 1;
}

/* ================================================================
 * GetTeamValueBase  (SWOS2.s line 28508)
 *
 * Given an average player price (byte), looks up the team value
 * base through two tables: avgTeamValues (byte→index) then
 * avgTeamValueBases (index→value).
 *
 * Asm register convention:
 *   in:  d0.b = avgPrice (ext.w sign-extends to word index)
 *   out: d0.w = team value base
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetTeamValueBase(uint16_t avgPrice)
{
    int16_t index = (int16_t)(int8_t)(avgPrice & 0xFF);  /* ext.w d0 */
    uint8_t tableVal = _avgTeamValues[index];
    return _avgTeamValueBases[tableVal];
}

/* ================================================================
 * CalcGateReceiptsBaseFromTeamValue  (SWOS2.s line 87365)
 *
 * Computes gate receipts base value from average player price.
 * Calls GetTeamValueBase then applies: base * 1000 / 768 * 1536.
 *
 * Asm register convention:
 *   in:  d0 = avgPrice (from GetAveragePlayerPrice)
 *   out: d0 = gate receipts base (d0.w after final mulu.w)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t CalcGateReceiptsBaseFromTeamValue(uint16_t avgPrice)
{
    uint16_t base = GetTeamValueBase(avgPrice);
    uint32_t val = (uint32_t)base * 1000;          /* mulu.w #1000,d0 */
    uint16_t quotient = (uint16_t)(val / 768);     /* divu.w #768,d0 → d0.w */
    uint32_t result = (uint32_t)quotient * 1536;   /* mulu.w #1536,d0 */
    return (uint16_t)result;
}

/* ================================================================
 * IsTeamNational  (SWOS2.s line 28526)
 *
 * Returns 1 if the team is "national" — either not in career mode,
 * or the team's continent byte is in [CN_EUROPE, CN_OCEANIA].
 * Returns 0 otherwise (career mode + non-national continent).
 *
 * Asm register convention:
 *   in:  a1 = pointer to team general info (continent at byte 0)
 *   out: d0.w = 1 (national) or 0 (not national), Z flag set if 0
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t IsTeamNational(TeamFileEx *team)
{
    if (_gameType != GAME_TYPE_CAREER)
        return 1;
    if (team->team.countryNumber < CN_EUROPE)
        return 0;
    if (team->team.countryNumber > CN_OCEANIA)
        return 0;
    return 1;
}

/* ================================================================
 * GetAveragePlayerPriceInSelectedTeams  (SWOS2.s line 28663)
 *
 * Computes the average player price across all selected teams
 * by calling GetAveragePlayerPrice for each team and dividing
 * the total by the number of selected teams.
 *
 * Asm register convention:
 *   out: d0.w = average price across all selected teams
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetAveragePlayerPriceInSelectedTeams(void)
{
    uint16_t i;
    uint16_t total;
    TeamFileEx *team;
    int32_t ext;

    if (_numSelectedTeams == 0)
        return 0;

    team = _selectedTeams;
    total = 0;
    for (i = _numSelectedTeams - 1; ; i--) {
        total += GetAveragePlayerPrice(team);
        team++;
        if (i == 0) break;
    }

    /* ext.l d0 + divu.w: sign-extend total to 32-bit then unsigned divide */
    ext = (int32_t)(int16_t)total;
    return (uint16_t)((uint32_t)ext / _numSelectedTeams);
}

/* ================================================================
 * GetPlayerRandomNum  (SWOS2.s line 28486)
 *
 * Computes a deterministic "random" number for a player by XORing
 * several player data bytes together with the current season and
 * random seed, then looking up the result in kRandomTable.
 *
 * Asm register convention:
 *   in:  a0 = pointer into team entry (offset by playerIndex * 38)
 *   out: d0.w = random number (0-255)
 *   clobbers: d1, a0
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetPlayerRandomNum(PlayerFile *playerFile)
{
    PlayerFile *pf = &playerFile[2];
    uint8_t d0, d1;

    d0 = (uint8_t)pf->price;                       /* move.b PLAYERFILE_PRICE+$4C(a0),d0 */
    d1 = pf->positionAndFace;                       /* move.b PLAYERFILE_POSITIONANDFACE+$4C(a0),d1 */
    d1 &= 0xFB;                                     /* andi.b #$FB,d1 */
    d0 ^= d1;                                       /* eor.b d1,d0 */
    d1 = (uint8_t)pf->playerName[0];               /* move.b PLAYERFILE_PLAYERNAME+$4C(a0),d1 */
    d0 ^= d1;                                       /* eor.b d1,d0 */
    d1 = (uint8_t)pf->playerName[10];              /* move.b PLAYERFILE_PLAYERNAME+$56(a0),d1 */
    d0 ^= d1;                                       /* eor.b d1,d0 */
    d1 = (uint8_t)_currentSeasonShowing;             /* move.w (currentSeasonShowing).l,d1; eor uses d1.b */
    d0 ^= d1;                                       /* eor.b d1,d0 */
    d1 = _newRandomSeedCopy;                         /* move.b (newRandomSeedCopy).l,d1 */
    d0 ^= d1;                                       /* eor.b d1,d0 */

    return (uint16_t)_kRandomTable[d0];              /* andi.w #$FF,d0; move.b (kRandomTable+d0.w),d0 */
}

/* ================================================================
 * GetDisplayedPlayerValue  (SWOS2.s line 28567)
 *
 * Returns the displayed player value (1-80) for the player at ptr.
 *
 * Two code paths:
 * - "Normal" path: if ptr is within currentTeam or careerTeam buffers,
 *   uses GetAgeAdjustedPlayerFormFactor + avgTeamValues lookup.
 * - "Random" path: for pointers outside those buffers, uses a
 *   randomized approach (GetPlayerRandomNum + delta table), though
 *   the random delta is ultimately discarded due to a move.w d2,d0
 *   overwrite in the original asm.
 *
 * Result is clamped to [1, 0x50].
 *
 * Asm register convention:
 *   in:  a0 = pointer into team data (adjusted by playerIndex * 38)
 *   out: d0.w = displayed value (1-80)
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetDisplayedPlayerValue(uint8_t *ptr)
{
    /* Check if ptr is within the currentTeam buffer (TeamFileEx + 16 reserves).
     * Bound on the buffer's own size, not the next BSS symbol: _playerPositions
     * was lifted out of the overlay (career_match_state.c), so it is no longer
     * &currentTeam + 1292. See docs/CAREERTEAM_STEP4_REWORK.md. */
    if (ptr >= _currentTeam && ptr < _currentTeam + CAREER_TEAM_BUF_SIZE)
        goto normal_path;

    /* Check if ptr is within the careerTeam buffer (TeamFileEx + 16 reserves). */
    if (ptr >= (uint8_t *)&_careerTeam && ptr < (uint8_t *)&_careerTeam + CAREER_TEAM_BUF_SIZE)
        goto normal_path;

    /* --- Random/career path (ptr not in currentTeam or careerTeam) --- */
    {
        PlayerFile *pf = (PlayerFile *)(ptr + 76);
        uint8_t priceByte = (uint8_t)pf->price;
        int16_t priceIndex = (int16_t)(int8_t)priceByte;  /* ext.w d2 */

        /* Note: asm calls GetPlayerRandomNum + displayedPlayerValueRandomDeltaTable
           lookup here, but the result is discarded by "move.w d2,d0" which overwrites
           d0.w with d2.w. The random delta does not affect the final value. */

        /* avgTeamValues lookup: move.b (avgTeamValues + d2.w),d2 changes only d2.b;
           d2 high byte is preserved from ext.w, so d2.w = (sign_byte : tableVal).
           Then move.w d2,d0 → d0.w = d2.w. For prices < 128, high byte is 0x00,
           so d0.w = tableVal. For prices >= 128, high byte is 0xFF, d0.w is negative. */
        if (priceIndex < 0) {
            return 1;   /* price >= 128: ext.w gives 0xFF high byte → negative → clamp to 1 */
        }

        {
            uint8_t tableVal = _avgTeamValues[priceIndex];
            /* d2.w = 0x00YY since priceIndex >= 0 → high byte from ext.w is 0x00 */
            uint16_t result = (uint16_t)tableVal;

            if ((int16_t)result < 1)
                return 1;
            if (result > 0x50)
                return 0x50;
            return result;
        }
    }

normal_path:
    /* --- Normal path (ptr within currentTeam or careerTeam) --- */
    {
        int16_t formFactor = GetAgeAdjustedPlayerFormFactor(ptr);
        PlayerFile *pf = (PlayerFile *)(ptr + 76);
        uint8_t priceByte = (uint8_t)pf->price;
        int16_t priceIndex = (int16_t)(int8_t)priceByte;  /* ext.w d0 */
        uint8_t tableVal = _avgTeamValues[priceIndex];      /* move.b (avgTeamValues + d0.w),d0 */
        /* After move.b, d0.b = tableVal, d0 high byte preserved from ext.w.
           But then add.w d1,d0 adds formFactor to the FULL word d0.w.
           d0.w after move.b = (sign_byte : tableVal). Then d0.w += formFactor. */
        int16_t d0w = (int16_t)(((uint16_t)priceIndex & 0xFF00) | tableVal);
        d0w += formFactor;                                 /* add.w d1,d0 */

        if (d0w < 1)
            return 1;
        if ((uint16_t)d0w > 0x50)
            return 0x50;
        return (uint16_t)d0w;
    }
}

/* ================================================================
 * InitPlayerValueDisplayStruct  (SWOS2.s line 28512)
 *
 * Computes the displayed player value, maps it to a sprite frame
 * index (0x71-0x7A), and writes a 4-word struct at dst+0x22.
 *
 * Asm register convention:
 *   in:  a0 = player entry pointer, a1 = dst struct pointer
 *   out: writes 4 words at dst+0x22..dst+0x28
 * ================================================================ */
__attribute__((used, externally_visible))
void InitPlayerValueDisplayStruct(uint8_t *playerEntry, uint8_t *dst)
{
    uint16_t val = GetDisplayedPlayerValue(playerEntry);   /* bsr GetDisplayedPlayerValue */
    val -= 1;                                               /* subq.w #1,d0 */
    val >>= 3;                                              /* lsr.w #3,d0 */
    val += 0x71;                                            /* addi.w #$71,d0 */

    /* Original asm wrote 4 uint16s at fixed offsets 0x22-0x28 into the
     * MenuEntry: content (= ENTRY_SPRITE2), stringFlags, fg low/high.
     * On 64-bit MenuEntry the bg slot is 4 bytes wider so content sits
     * at offset 0x26 (not 0x22) and fg at 0x2A (not 0x26) — use the
     * typed fields so the writes land correctly regardless of bitness. */
    MenuEntry *e = (MenuEntry *)dst;
    e->content     = 3;     /* ENTRY_SPRITE2 — show a sprite by idx in fg low 16 */
    e->stringFlags = 0;
    e->fgWord = val;
}

/* ================================================================
 * SnapToNearestAvgTeamValueBase  (SWOS2.s line 71597)
 *
 * Scans avgTeamValueBases for the nearest entry to the given value.
 * Uses midpoint rounding: at the exact midpoint between two entries,
 * rounds UP (returns the higher entry) because lsr.w #1 truncates.
 *
 * Asm register convention:
 *   in:  d0.w = value
 *   out: d0.w = nearest avgTeamValueBases entry
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t SnapToNearestAvgTeamValueBase(uint16_t value)
{
    uint16_t *p;
    uint16_t upper, lower, midpoint;

    if (value == 0)
        return 0;

    p = _avgTeamValueBases;

    /* Scan until we find an entry >= value */
    while (value > *p)                          /* cmp.w (a0)+,d0; bhi loop */
        p++;

    upper = *p;                                 /* -2(a0) after post-increment */
    lower = *(p - 1);                           /* -4(a0) after post-increment */

    midpoint = ((upper - lower) >> 1) + lower;  /* lsr.w #1 = truncating /2 */

    if (value < midpoint)                       /* cmp.w d1,d0; bcs → return lower */
        return lower;

    return upper;                               /* value >= midpoint → return upper */
}

/* ================================================================
 * SnapUpToAvgTeamValueBase  (SWOS2.s line 71619)
 *
 * Scans avgTeamValueBases for the first entry >= value.
 *
 * Asm register convention:
 *   in:  d0.w = value
 *   out: d0.w = first avgTeamValueBases entry >= value
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t SnapUpToAvgTeamValueBase(uint16_t value)
{
    uint16_t *p;

    if (value == 0)
        return 0;

    p = _avgTeamValueBases;

    /* Scan until we find an entry >= value */
    while (value > *p)                          /* cmp.w (a0)+,d0; bhi loop */
        p++;

    return *p;                                  /* -2(a0) after post-increment */
}

/* ================================================================
 * RotatePlayersLeft  (SWOS2.s line 59148)
 *
 * Rotates the substitute player slots left by one position.
 * The first substitute (at offset maxSubs + 11) moves to the end,
 * and all other substitutes shift left.
 *
 * Register convention:
 *   in:  a0 = team pointer
 *        d0 = maximum number of substitutes
 *
 * If teamPtr == &currentTeam, uses _playerPositions array.
 * If teamPtr == &careerTeam, uses currentMatchPlayers array.
 * Otherwise, uses teamPtr + 0x3C (TEAMFILE_PLAYERS).
 * ================================================================ */
extern uint8_t _currentMatchPlayers[];
extern uint16_t _numPlayers;

__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void RotatePlayersLeft(TeamFileEx *team, uint16_t maxSubs)
{
    uint8_t *arr;
    int16_t count;

    if ((uint8_t *)team == _currentTeam) {
        arr = _playerPositions;
        count = (int16_t)_numPlayers;
    } else if (team == &_careerTeam) {
        arr = _currentMatchPlayers;
        count = (int16_t)*(uint16_t *)_careerNumPlayers;
    } else {
        arr = team->team.playerNumbers;
        count = 0x10;
    }

    uint16_t offset = maxSubs + 11;
    arr += offset;
    count -= (int16_t)offset;
    count -= 2;

    /* Rotate left: save first element, shift remaining, put saved at end */
    uint8_t saved = arr[0];
    for (int16_t i = count; i >= 0; i--) {
        arr[0] = arr[1];
        arr++;
    }
    *arr = saved;
}

/* ================================================================
 * AddTeamToBuffer  (SWOS2.s line 40491)
 *
 * Copies a 684-byte team record to the end of the selTeamsPtr
 * buffer, increments numSelectedTeams, and returns a pointer to
 * the newly added entry.
 *
 * Asm register convention:
 *   in:  a0 = source team pointer
 *   out: a0 = pointer to copy in selTeamsPtr buffer
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
TeamFileEx *AddTeamToBuffer(TeamFileEx *team)
{
    TeamFileEx *dest = _selectedTeams + _numSelectedTeams;
    uint16_t *src16 = (uint16_t *)team;
    uint16_t *dst16 = (uint16_t *)dest;
    uint16_t i;
    for (i = 342; i != 0; i--)
        *dst16++ = *src16++;
    _numSelectedTeams++;

    return dest;
}

/* ================================================================
 * FindTeam  (SWOS2.s line 40528)
 *
 * Searches the selTeamsPtr buffer for a team with the same ID
 * (first word) as the given team. Returns a pointer to the found
 * entry and the remaining entry count via output pointer.
 * Panics if not found.
 *
 * Asm register convention:
 *   in:  a0 = team to search for (uses first word as ID)
 *   out: a0 = found team in selTeamsPtr buffer
 *        d2 = remaining entries after found one
 * ================================================================ */
__attribute__((used, externally_visible))
TeamFileEx *FindTeam(TeamFileEx *teamToFind, uint16_t *outRemainingCount)
{
    /* teamId is the BE-packed (country<<8|ordinal) form used by the
     * loop comparison below. Pre-fix this read was *(uint16_t *)
     * teamToFind which on LE gives (ordinal<<8|country) — once the
     * loop side was converted to PACK_BE_U16 in commit 1eab85d the
     * two sides stopped matching and selecting any team for friendly
     * panicked with reasonCode 0x6987. */
    uint16_t teamId = PACK_BE_U16(((uint8_t *)teamToFind)[0],
                                  ((uint8_t *)teamToFind)[1]);
    uint16_t count = _numSelectedTeams;
    if (count == 0)
        goto panic;
    count--;
    TeamFileEx *ptr = _selectedTeams;
    uint16_t remaining = count;
    while (1) {
        /* First 2 bytes of each TeamFileEx are {countryNumber, teamOrdinal}.
         * teamId is BE-packed (country<<8 | ordinal); read the byte pair
         * via PACK_BE_U16 to get the same packed form on both hosts —
         * *(uint16_t *)ptr would give (ordinal<<8|country) on LE. */
        if (PACK_BE_U16(((uint8_t *)ptr)[0], ((uint8_t *)ptr)[1]) == teamId) {
            if (outRemainingCount)
                *outRemainingCount = remaining;
            return ptr;
        }
        ptr++;
        if (remaining == 0)
            break;
        remaining--;
    }
panic:
    _panicReasonCode = 0x6987;
    PanicHaltLoop();
    /* never reached */
    *outRemainingCount = 0;
    return (TeamFileEx *)0;
}

/* ================================================================
 * DeselectTeam  (SWOS2.s line 40507)
 *
 * Removes a team from the selTeamsPtr buffer by finding it,
 * shifting remaining entries down, and decrementing numSelectedTeams.
 *
 * Asm register convention:
 *   in:  a0 = team to remove
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void DeselectTeam(TeamFileEx *teamToRemove)
{
    uint16_t remaining;
    TeamFileEx *found = FindTeam(teamToRemove, &remaining);
    if (remaining != 0) {
        uint16_t *dst = (uint16_t *)found;
        uint16_t *src = (uint16_t *)(found + 1);
        uint32_t words = (uint32_t)remaining * 342;
        while (words != 0) {
            *dst++ = *src++;
            words--;
        }
    }
    _numSelectedTeams--;
}

/* ================================================================
 * Chunk 91 — Team/Player Lookup & National Selector Functions
 *
 * GetPlayer, GetPlayerAtIndex, GetPlayerSlotIndexByPlayerNumber:
 * foundational player record lookup used throughout the game.
 *
 * National selector functions:
 * IsTeamCodeOkForNationalSelector, IsPlayerOkForNationalSelector,
 * GetNationalityAndTeamFile, GetNationalTeamNumberFromTeamFileId,
 * GetNationalTeamNumber, GetNationalityRecordPtr.
 * ================================================================ */

/* ================================================================
 * GetPlayer — get pointer to player record in team file data.
 * Asm: GetPlayer (d0=teamNumber, d1=playerFileIndex → a0=playerPtr)
 *
 * Returns: teamBase + playerFileIndex * sizeof(PlayerFile).
 * Callers add 76 (sizeof(TeamFile)) to reach actual player data fields.
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerFileIndex)
{
    uint8_t *teamBase;

    if (teamNumber == _currentTeamNumber)
        teamBase = _currentTeam;
    else if (teamNumber == _selTeamNumber)
        teamBase = (uint8_t *)&_careerTeam;
    else
        teamBase = (uint8_t *)GetTeamPointer(teamNumber);

    return teamBase + (uint16_t)(playerFileIndex * sizeof(PlayerFile));
}

/* ================================================================
 * GetPlayerAtIndex — get player pointer from team number + ordinal.
 * Asm: GetPlayerAtIndex (d0=teamNumber, d1=ordinal → a0=playerPtr, d1=fileIndex)
 *
 * Remaps ordinal through position table (_playerPositions for currentTeam,
 * currentMatchPlayers for selTeam, TEAMFILE_PLAYERNUMBERS for generic teams),
 * then calls GetPlayer with the remapped file index.
 *
 * Side effect: writes remapped file index to _cGetPlayerAtIndexFileIdx.
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal)
{
    uint16_t fileIndex;

    if (teamNumber == _currentTeamNumber) {
        fileIndex = (uint16_t)_playerPositions[ordinal];
    } else if (teamNumber == _selTeamNumber) {
        fileIndex = (uint16_t)_currentMatchPlayers[ordinal];
    } else {
        if (ordinal >= 16) {
            _panicReasonCode = 0x1146;
            PanicHaltLoop();
        }
        TeamFileEx *teamPtr = GetTeamPointer(teamNumber);
        fileIndex = (uint16_t)teamPtr->team.playerNumbers[ordinal];
    }

    _cGetPlayerAtIndexFileIdx = fileIndex;
    return GetPlayer(teamNumber, fileIndex);
}

/* ================================================================
 * GetPlayerSlotIndexByPlayerNumber — reverse lookup: player number → slot index.
 * Asm: GetPlayerSlotIndexByPlayerNumber (d0=teamNumber, d1=playerNumber → d1=slotIndex)
 *
 * For selTeamNumber: searches currentMatchPlayers[], panics if index >= careerNumPlayers.
 * For other teams: validates playerNumber < 16, searches team[TEAMFILE_PLAYERNUMBERS+i].
 * ================================================================ */
__attribute__((used, externally_visible))
uint16_t GetPlayerSlotIndexByPlayerNumber(uint16_t teamNumber, uint16_t playerNumber)
{
    uint16_t idx = 0;

    if (teamNumber == _selTeamNumber) {
        /* Search currentMatchPlayers for matching player number */
        uint16_t limit = *(uint16_t *)_careerNumPlayers;
        while (_currentMatchPlayers[idx] != (uint8_t)playerNumber) {
            idx++;
            if (idx >= limit) {
                _panicReasonCode = 0x2617;
                PanicHaltLoop();
            }
        }
    } else {
        /* Validate input */
        if (playerNumber >= 16) {
            _panicReasonCode = 0x2618;
            PanicHaltLoop();
        }
        /* Search team's player number table */
        TeamFileEx *teamPtr = GetTeamPointer(teamNumber);
        while (teamPtr->team.playerNumbers[idx] != (uint8_t)playerNumber) {
            idx++;
            if (idx >= 16) {
                _panicReasonCode = 0x2619;
                PanicHaltLoop();
            }
        }
    }

    return idx;
}

/* ================================================================
 * GetNationalityRecordPtr — find nationality record by team file+ordinal.
 * Asm: GetNationalityRecordPtr (d0=teamFileAndOrdinal → a0=recordPtr or NULL)
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *GetNationalityRecordPtr(uint16_t teamFileAndOrdinal)
{
    NationalTeamRecord *rec = (NationalTeamRecord *)_nationalTeams;
    uint16_t count = _numNationalTeams;
    for (uint16_t i = 0; i < count; i++) {
        /* countryNumber/teamOrdinal are a BE byte pair (caller passes
         * the BE-packed pair). PACK_BE_U16 produces the matching id on
         * either host endianness. */
        if (PACK_BE_U16(rec->countryNumber, rec->teamOrdinal) == teamFileAndOrdinal)
            return (uint8_t *)rec;
        rec++;
    }
    return (uint8_t *)0;
}

/* ================================================================
 * GetNationalTeamNumber — find national team number by nationality code.
 * Asm: GetNationalTeamNumber (d0=nationality → d0=teamNumber or -1)
 *
 * Searches nationalTeams[i][2] for matching nationality byte.
 * Returns the team file+ordinal word from the matching record, or 0xFFFF.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t GetNationalTeamNumber(uint16_t nationality)
{
    NationalTeamRecord *rec = (NationalTeamRecord *)_nationalTeams;
    uint16_t count = _numNationalTeams;
    for (uint16_t i = 0; i < count; i++) {
        if (rec->nationalityCode == (uint8_t)nationality)
            /* The packed national-team ID is the BE byte pair
             * {countryNumber, teamOrdinal} — recompose via PACK_BE_U16
             * (was: BE_READ_U16(ptr) — same result, this is the
             * struct-field form). */
            return (int16_t)PACK_BE_U16(rec->countryNumber, rec->teamOrdinal);
        rec++;
    }
    return -1;
}

/* ================================================================
 * GetNationalTeamNumberFromTeamFileId — find national team number by team file ID.
 * Asm: GetNationalTeamNumberFromTeamFileId (d0=teamFileId → d0=teamNumber or -1)
 *
 * Searches nationalTeams[i][3] for matching team file ID byte.
 * Returns the team file+ordinal word from the matching record, or 0xFFFF.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t GetNationalTeamNumberFromTeamFileId(uint16_t teamFileId)
{
    NationalTeamRecord *rec = (NationalTeamRecord *)_nationalTeams;
    uint16_t count = _numNationalTeams;
    for (uint16_t i = 0; i < count; i++) {
        if (rec->teamFileId == (uint8_t)teamFileId)
            /* Asm: `move.w (a0),d0` — on m68k BE this reads the
             * `(country<<8)|ordinal` packed national-team ID stored as
             * two adjacent bytes [country, ordinal]. Use PACK_BE_U16
             * so the same id materialises on either host endianness
             * (Playaveli's NATIONAL TEAM job-offer SIGSEGV was the
             * naive host-native read of these two bytes). */
            return (int16_t)PACK_BE_U16(rec->countryNumber, rec->teamOrdinal);
        rec++;
    }
    return -1;
}

/* ================================================================
 * GetNationalityAndTeamFile — look up nationality and team file ID.
 * Asm: GetNationalityAndTeamFile (d0=teamFileAndOrdinal → d0=nationality, d1=teamFileId)
 *
 * Returns packed uint32_t: (nationality << 16) | teamFileId.
 * Asm bridge unpacks via swap+move.
 * Panics (0x4543) if not found.
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t GetNationalityAndTeamFile(uint16_t teamFileAndOrdinal)
{
    NationalTeamRecord *rec = (NationalTeamRecord *)GetNationalityRecordPtr(teamFileAndOrdinal);
    if (!rec) {
        _panicReasonCode = 0x4543;
        PanicHaltLoop();
    }
    return ((uint32_t)rec->nationalityCode << 16) | (uint32_t)rec->teamFileId;
}

/* ================================================================
 * IsTeamCodeOkForNationalSelector — check if team code allows national selection.
 * Asm: IsTeamCodeOkForNationalSelector (d0=code → Z flag from btst)
 *
 * Returns: 0 = not OK, non-zero = OK.
 * If not in job offer mode and currentSeasonShowing < 3, returns 0.
 * Otherwise tests bit currentSeasonShowing in the appropriate season mask
 * (mask A for code 'P', mask B for other codes).
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsTeamCodeOkForNationalSelector(uint16_t code)
{
    /* Early exit if not in job offer mode and season < 3 */
    if (_careerJobOfferMode == 0 && _currentSeasonShowing < 3)
        return 0;

    /* Pick mask based on code */
    uint32_t mask;
    if ((uint8_t)code == 0x50)  /* 'P' */
        mask = _nationalSelectorSeasonMaskA;
    else
        mask = _nationalSelectorSeasonMaskB;

    /* Test bit currentSeasonShowing in mask */
    uint16_t bit = _currentSeasonShowing;
    return (int16_t)((mask >> bit) & 1);
}

/* ================================================================
 * IsPlayerOkForNationalSelector — check if player national selection is possible.
 * Asm: IsPlayerOkForNationalSelector (→ Z flag)
 *
 * Returns: 0 = not OK, non-zero = OK.
 * If not in job offer mode and season < 3, returns 0.
 * Otherwise returns OK if bit currentSeasonShowing is set in either mask A or mask B.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t IsPlayerOkForNationalSelector(void)
{
    /* Early exit if not in job offer mode and season < 3 */
    if (_careerJobOfferMode == 0 && _currentSeasonShowing < 3)
        return 0;

    uint16_t bit = _currentSeasonShowing;
    /* Check mask A first */
    if ((_nationalSelectorSeasonMaskA >> bit) & 1)
        return 1;
    /* Check mask B */
    return (int16_t)((_nationalSelectorSeasonMaskB >> bit) & 1);
}

/* ================================================================
 * Chunk 95 — SetBenchPlayersNumbers, Career_RemovePlayerFromSelectedTeamRoster,
 *            RemovePlayerFromTeam, replacement helpers
 * ================================================================ */

extern uint8_t *GetPlayerAtIndex(uint16_t teamNumber, uint16_t ordinal);
extern void Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages(uint16_t playerIndex);
extern void Career_UpdatePlayerWeeklyWageCache(uint16_t playerIndex);
extern void TrainingMatch_RemoveSavedPlayerId(uint16_t value);
extern void Career_RemovePlayerFromTeamInternal(uint16_t teamNumber, uint16_t playerOrd);
extern void PrunePlayerOffersAgainstRosterLimits(void);

/* asm bridges for functions not yet ported to C */
extern void Career_GenerateTrialistForTeamPlayer(uint16_t playerIndex);

/* asm data externs */
extern uint16_t _removedPlayerStatAccum;


/* ================================================================
 * SetBenchPlayersNumbers — assign shirt numbers 12-16 to bench players.
 * Asm: SetBenchPlayersNumbers (a0=team ptr)
 *
 * Loops players at ordinal index 11-15. For each, calls GetPlayerAtIndex
 * to get the player record pointer, then writes (ordinal + 1) as the
 * shirt number at offset PF_SHIRTNUM from that raw pointer.
 * ================================================================ */
__attribute__((used, externally_visible))
void SetBenchPlayersNumbers(TeamFileEx *team)
{
    uint16_t teamNum = BE_READ_U16(team);  /* first word: countryNumber + teamOrdinal */
    uint16_t d1;

    for (d1 = 11; d1 < 16; d1++) {
        uint8_t *player = GetPlayerAtIndex(teamNum, d1);
        PlayerFile *pf = (PlayerFile *)(player + 76);
        pf->shirtNumber = (int8_t)(d1 + 1);
    }
}

/* ================================================================
 * Replacement position search order tables (from FindReplacementPlayerByPositionOrder)
 * Position codes: 0=GK, 1=RB, 2=LB, 3=Def, 4=RW, 5=LW, 6=Mid, 7=Att
 * ================================================================ */
static const uint8_t replPosSearchOrderGK[]  = {0,3,6,7,1,2,4,5,0xFF};
static const uint8_t replPosSearchOrderRB[]  = {1,3,2,4,6,5,7,0,0xFF};
static const uint8_t replPosSearchOrderLB[]  = {2,3,1,5,6,4,7,0,0xFF};
static const uint8_t replPosSearchOrderDef[] = {3,1,2,6,4,5,7,0,0xFF};
static const uint8_t replPosSearchOrderRW[]  = {4,6,5,3,1,2,7,0,0xFF};
static const uint8_t replPosSearchOrderLW[]  = {5,6,4,3,2,1,7,0,0xFF};
static const uint8_t replPosSearchOrderMid[] = {6,4,5,3,1,2,7,0,0xFF};
static const uint8_t replPosSearchOrderAtt[] = {7,6,4,5,3,1,2,0,0xFF};

static const uint8_t *replPosSearchOrderTable[8] = {
    replPosSearchOrderGK,
    replPosSearchOrderRB,
    replPosSearchOrderLB,
    replPosSearchOrderDef,
    replPosSearchOrderRW,
    replPosSearchOrderLW,
    replPosSearchOrderMid,
    replPosSearchOrderAtt,
};

/* ================================================================
 * findReplacementPlayerByPositionOrder — find best replacement in a range.
 * Asm: FindReplacementPlayerByPositionOrder
 *   d1=removedPlayerOrdinal, d5=rangeStart, d6=rangeEnd
 *   Returns best candidate ordinal in d2, also sets a3=player ptr.
 *
 * Priority: match position, then prefer a REGULAR player over a TRIAL/RES
 * one ($9B/$9A form), then the highest price byte ($6C); on a tie the first
 * one found stays. The asm (SWOS2.s FindReplacementPlayerByPositionOrder):
 *   candidate TRIAL/RES, best regular  -> bra skip
 *   candidate regular,   best TRIAL/RES -> take it (loc_1421F8)
 *   same kind                           -> cmp.b $6C(a0),d3 / bge skip
 * An earlier port had the first two the wrong way round, so selling a
 * starter promoted a trialist off the bench ahead of a better regular.
 * ================================================================ */
static uint16_t findReplacementPlayerByPositionOrder(
    uint16_t removedOrdinal, uint16_t rangeStart, uint16_t rangeEnd)
{
    /* Get the removed player to determine its position */
    uint8_t *removedPlayer = GetPlayerAtIndex(_selTeamNumber, removedOrdinal);
    PlayerFile *removedPf = (PlayerFile *)(removedPlayer + 76);
    uint16_t posIdx = (uint16_t)((removedPf->positionAndFace & PF_POSITION_MASK) >> 5);
    const uint8_t *searchOrder = replPosSearchOrderTable[posIdx];

    while (1) {
        uint8_t desiredPos = *searchOrder++;
        if (desiredPos == 0xFF) {
            /* Exhausted all position priorities — panic */
            _panicReasonCode = 0x2719;
            PanicHaltLoop();
        }

        int8_t bestPrice = -1;       /* d3: move.b #$FF */
        uint16_t bestIdx = 0xFFFF;   /* d2 */
        PlayerFile *bestPf = (void *)0;   /* a3 */

        uint16_t i;
        for (i = rangeStart; i < rangeEnd; i++) {
            uint8_t *candidate = GetPlayerAtIndex(_selTeamNumber, i);
            PlayerFile *candPf = (PlayerFile *)(candidate + 76);
            uint8_t candPos = (uint8_t)((candPf->positionAndFace & PF_POSITION_MASK) >> 5);

            if (candPos != desiredPos)
                continue;

            /* tst.b d3 / bmi: with no candidate yet there is no a3 to classify,
             * so go straight to the price compare. */
            if (bestPrice >= 0) {
                int16_t candTrial = (uint8_t)candPf->form == PLAYER_FORM_TRIALIST ||
                                    (uint8_t)candPf->form == PLAYER_FORM_RESERVE;
                int16_t bestTrial = (uint8_t)bestPf->form == PLAYER_FORM_TRIALIST ||
                                    (uint8_t)bestPf->form == PLAYER_FORM_RESERVE;
                if (candTrial && !bestTrial)
                    continue;       /* a regular player is never displaced by a trialist */
                if (!candTrial && bestTrial)
                    goto accept;    /* ...and always displaces one, whatever the price */
            }

            /* Same kind: cmp.b $6C(a0),d3 / bge skip — signed, higher price wins */
            if (bestPrice >= candPf->price)
                continue;

        accept:
            bestIdx = i;
            bestPrice = candPf->price;
            bestPf = candPf;
        }

        if (bestIdx != 0xFFFF) {
            return bestIdx;
        }
        /* No match for this position — try next priority */
    }
}

/* ================================================================
 * Career_RemovePlayerFromSelectedTeamRoster — remove player at given index.
 * Asm: Career_RemovePlayerFromSelectedTeamRoster (d1=index)
 *
 * Accumulates 4 stat bytes from removed player into removedPlayerStatAccum.
 * Three paths based on slot tier:
 * - Starting (0-10): find bench replacement, swap in
 * - Bench (11-15): find reserve replacement or generate trialist if at 16
 * - Reserve (16+): straight array compaction
 * ================================================================ */
__attribute__((used, externally_visible))
void Career_RemovePlayerFromSelectedTeamRoster(uint16_t index)
{
    uint8_t *player;
    uint16_t d0, d1, d2, d3;
    uint8_t *a0;
    uint8_t *a1;

    d1 = index;

    /* Get player record and accumulate stats */
    player = GetPlayer(_selTeamNumber, d1);
    {
        PlayerFile *pf = (PlayerFile *)(player + 76);
        d0 = (uint16_t)pf->goalStats[0];
        _removedPlayerStatAccum += d0;
        d0 = (uint16_t)pf->goalStats[1];
        _removedPlayerStatAccum += d0;
        d0 = (uint16_t)pf->goalStats[2];
        _removedPlayerStatAccum += d0;
        d0 = (uint16_t)pf->goalStats[3];
        _removedPlayerStatAccum += d0;
    }

    /* Remove weekly wage */
    Career_RemovePlayerWeeklyWageAndRecalcWeeklyWages(d1);

    /* Get slot index by player number */
    d1 = GetPlayerSlotIndexByPlayerNumber(_selTeamNumber, d1);

    if (d1 < 0x0B) {
        /* Starting player (slots 0-10) */
        d2 = d1;

        /* Find bench replacement */
        d2 = findReplacementPlayerByPositionOrder(d1, 0x0B, 0x10);

        /* Swap in currentMatchPlayers */
        {
            uint8_t tmp = _currentMatchPlayers[d2];
            _currentMatchPlayers[d2] = _currentMatchPlayers[d1];
            _currentMatchPlayers[d1] = tmp;
        }

        /* Swap shirt numbers between the two */
        {
            uint8_t *p1 = GetPlayer(_selTeamNumber, (uint16_t)(int8_t)_currentMatchPlayers[d1]);
            a1 = p1;
            uint8_t *p2 = GetPlayer(_selTeamNumber, (uint16_t)(int8_t)_currentMatchPlayers[d2]);
            PlayerFile *pf1 = (PlayerFile *)(a1 + 76);
            PlayerFile *pf2 = (PlayerFile *)(p2 + 76);
            int8_t tmpShirt = pf2->shirtNumber;
            pf2->shirtNumber = pf1->shirtNumber;
            pf1->shirtNumber = tmpShirt;
        }

        d1 = d2;  /* now d1 = bench slot to be removed */
        /* Fall through to bench path */
        goto bench_path;
    }

    if (d1 < 0x10) {
        /* Bench player (slots 11-15) */
        d2 = d1;
    bench_path:
        if (*(uint16_t *)_careerNumPlayers == 16) {
            /* Exactly 16 players — generate trialist replacement */
            d1 = (uint16_t)(int8_t)_currentMatchPlayers[d2];
            Career_GenerateTrialistForTeamPlayer(d1);
            Career_UpdatePlayerWeeklyWageCache(d1);
            return;
        }

        /* Find reserve replacement */
        d3 = d2;
        {
            /* SelectReserveReplacementForBenchPlayer: range 16..careerNumPlayers */
            d1 = d2;
            d2 = findReplacementPlayerByPositionOrder(d1, 0x10, *(uint16_t *)_careerNumPlayers);
        }

        /* Swap in currentMatchPlayers: d3 is bench slot, d2 is reserve slot */
        {
            uint8_t tmp = _currentMatchPlayers[d3];
            _currentMatchPlayers[d3] = _currentMatchPlayers[d2];
            _currentMatchPlayers[d2] = tmp;
        }

        /* Swap shirt numbers */
        {
            uint8_t *p1 = GetPlayer(_selTeamNumber, (uint16_t)(int8_t)_currentMatchPlayers[d2]);
            a1 = p1;
            uint8_t *p2 = GetPlayer(_selTeamNumber, (uint16_t)(int8_t)_currentMatchPlayers[d3]);
            PlayerFile *pf1 = (PlayerFile *)(a1 + 76);
            PlayerFile *pf2 = (PlayerFile *)(p2 + 76);
            int8_t tmpShirt = pf2->shirtNumber;
            pf2->shirtNumber = pf1->shirtNumber;
            pf1->shirtNumber = tmpShirt;
        }

        /* Asm reads d1 from the reserve slot (where the to-remove player
         * now lives after the swap). In this C function the variable
         * mapping is flipped vs the asm — d3 holds the bench slot here,
         * d2 holds the reserve slot — so reorder: reassign d3 = reserve
         * first, then read d1. Reading currentMatchPlayers[d3] before
         * the reassignment grabbed the REPLACEMENT's ord instead, which
         * caused TrainingMatch_RemoveSavedPlayerId() to panic 0x1231 on
         * ACCEPT OFFER (replacement reserve not in saved-players list)
         * and *ERR*-stomped the wrong player's name. */
        d3 = d2;  /* d3 = reserve slot to compact from */
        d1 = (uint16_t)(int8_t)_currentMatchPlayers[d3];
        goto compact;
    }

    /* Reserve player (slots 16+) */
    d3 = d1;
    d1 = (uint16_t)(int8_t)_currentMatchPlayers[d1];

compact:
    /* Shift currentMatchPlayers array to fill the gap at d3 */
    {
        uint16_t count = *(uint16_t *)_careerNumPlayers - d3;
        volatile uint8_t *va0 = &_currentMatchPlayers[d3];
        if (count >= 2) {
            uint16_t i;
            for (i = 0; i < count - 1; i++) {
                va0[i] = va0[i + 1];
            }
        }
        /* Sentinel at end */
        va0[count >= 2 ? count - 1 : 0] = 0xFF;
    }

    /* Write "*ERR* " into the removed player's name slot */
    {
        uint8_t *pRem = GetPlayer(_selTeamNumber, d1);
        PlayerFile *pf = (PlayerFile *)(pRem + 76);
        pf->playerName[0] = '*';
        pf->playerName[1] = 'E';
        pf->playerName[2] = 'R';
        pf->playerName[3] = 'R';
        pf->playerName[4] = '*';
        pf->playerName[5] = ' ';
    }

    TrainingMatch_RemoveSavedPlayerId(d1);
    *(uint16_t *)_careerNumPlayers -= 1;
}

/* ================================================================
 * RemovePlayerFromTeam — wrapper that removes player from team data
 * and from the roster, sets dirty flag, prunes offers.
 * Asm: RemovePlayerFromTeam (d1=player ordinal, uses selTeamNumber)
 * ================================================================ */
__attribute__((used, externally_visible))
void RemovePlayerFromTeam(uint16_t playerOrd)
{
    Career_RemovePlayerFromTeamInternal(_selTeamNumber, playerOrd);
    Career_RemovePlayerFromSelectedTeamRoster(playerOrd);
    _careerTeam.team.playerValuesDirty = TEAM_VALUES_DIRTY;
    PrunePlayerOffersAgainstRosterLimits();
}

/* ================================================================
 * DeselectAllTeams (Chunk 107)
 * Removes all selectable teams from the selected teams buffer.
 * Teams with teamControls != 0 are skipped (not deselected).
 * Asm: DeselectAllTeams
 * ================================================================ */
__attribute__((used, externally_visible))
void DeselectAllTeams(void)
{
    TeamFileEx *ptr = _selectedTeams;
    if (_numSelectedTeams == 0)
        return;

    uint16_t idx = 0;
    while (idx < _numSelectedTeams) {
        if (ptr->team.teamControls == 0) {
            /* DeselectTeam removes this entry and shifts the rest down,
             * so don't advance the pointer or index. */
            DeselectTeam(ptr);
        } else {
            ptr++;
            idx++;
        }
    }
}

/* ================================================================
 * ChangeTeams (Chunk 107)
 * Menu callback: deselect all teams, flag "change teams", exit menu.
 * Asm: ChangeTeams
 * ================================================================ */
__attribute__((used, externally_visible))
void ChangeTeams(void)
{
    DeselectAllTeams();
    _changeTeams = 1;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * AbortChooseTeamsFriendly (Chunk 107)
 * Menu callback: deselect all, flag abort, exit menu.
 * Asm: AbortChooseTeamsFriendly
 * ================================================================ */
__attribute__((used, externally_visible))
void AbortChooseTeamsFriendly(void)
{
    DeselectAllTeams();
    _abortSelectTeams = 1;
    _changeTeams = 0;
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * ChooseTeamsFriendlyOnSelect76 (Chunk 107)
 * Menu callback: deselect all, flag result chosen, exit menu.
 * Asm: ChooseTeamsFriendlyOnSelect76
 * ================================================================ */
__attribute__((used, externally_visible))
void ChooseTeamsFriendlyOnSelect76(void)
{
    DeselectAllTeams();
    _chooseTeamsResult = 1;
    _changeTeams = 0;
    _viewSelectedTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * ViewSelected (Chunk 108)
 * Menu callback: flag "view selected teams", clear others, exit menu.
 * Asm: ViewSelected
 * ================================================================ */
__attribute__((used, externally_visible))
void ViewSelected(void)
{
    _viewSelectedTeams = 1;
    _chooseTeamsResult = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * ExitChooseTeams2 (Chunk 108)
 * Menu callback: exit team selection. If topOfStackMark >= 0, sets
 * exitSelectTeams=1; otherwise sets abortSelectTeams=1.
 * Asm: ExitChooseTeams2
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitChooseTeams2(void)
{
    _chooseTeamsResult = 0;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    if (_topOfStackMark >= 0) {
        _exitSelectTeams = 1;
        _abortSelectTeams = 0;
    } else {
        _abortSelectTeams = 1;
        _exitSelectTeams = 0;
    }
    SetExitMenuFlag();
}

/* ================================================================
 * ChooseTeamsPlayMatchSelected (Chunk 108)
 * Menu callback: flag "play match selected", clear others, exit menu.
 * Asm: ChooseTeamsPlayMatchSelected
 * ================================================================ */
__attribute__((used, externally_visible))
void ChooseTeamsPlayMatchSelected(void)
{
    _chooseTeamsResult = 1;
    _viewSelectedTeams = 0;
    _changeTeams = 0;
    _abortSelectTeams = 0;
    _exitSelectTeams = 0;
    SetExitMenuFlag();
}

/* ================================================================
 * Team navigation stack & contest checking (Chunk 109)
 * Asm: GetCountryName, PushContinent, PopContinent,
 *      SetSelectedTeamsStackTop, PushLastSelectedTeam,
 *      GetSelectedTeamsStackTop, MarkSelectedTeamsStackLocation,
 *      IsContestContinentalCup, SetTeamsTypeForSelectedContest,
 *      IsClubTeamsContest
 * ================================================================ */

/* GetCountryName: look up country name from countriesTable.
 * countriesTable[d0] is a pointer to a length-prefixed string; skip byte 0. */
__attribute__((used, externally_visible))
uint8_t *GetCountryName(uint16_t countryId)
{
    uint8_t *entry = (uint8_t *)_countriesTable[countryId];
    uint8_t *name = entry + 1;  /* skip length byte */
    _curCountryStr = name;
    return name;
}

/* PushContinent: push country id onto continent stack */
__attribute__((used, externally_visible))
void PushContinent(uint16_t countryId)
{
    uint8_t *ptr = _countryStackPtr;
    *ptr++ = (uint8_t)countryId;
    _countryStackPtr = ptr;
    _teamDepthLevel++;
}

/* PopContinent: pop country id from continent stack */
__attribute__((used, externally_visible))
uint16_t PopContinent(void)
{
    uint8_t *ptr = _countryStackPtr;
    ptr--;
    uint8_t val = *ptr;
    *ptr = CN_OCEANIA;  /* sentinel value */
    _countryStackPtr = ptr;
    _teamDepthLevel--;
    return (uint16_t)val;
}

/* SetSelectedTeamsStackTop: write -1 at current depth in selectedTeamsStack */
__attribute__((used, externally_visible))
void SetSelectedTeamsStackTop(void)
{
    _selectedTeamsStack[_teamDepthLevel] = 0xFFFF;
}

/* PushLastSelectedTeam: store lastSelectedTeam at current depth */
__attribute__((used, externally_visible))
void PushLastSelectedTeam(void)
{
    _selectedTeamsStack[_teamDepthLevel] = _lastSelectedTeam;
}

/* GetSelectedTeamsStackTop: read from stack into prevSelectedTeamEntry */
__attribute__((used, externally_visible))
void GetSelectedTeamsStackTop(void)
{
    _prevSelectedTeamEntry = _selectedTeamsStack[_teamDepthLevel];
}

/* MarkSelectedTeamsStackLocation: set topOfStackMark based on state flags */
__attribute__((used, externally_visible))
void MarkSelectedTeamsStackLocation(void)
{
    _topOfStackMark = (int16_t)_teamDepthLevel;

    if (_isCountryNumber != 0)
        _topOfStackMark--;

    if (_gotCountryName != 0 && _numberOfLeagues == 1) {
        _topOfStackMark = -1;
        return;
    }

    if (_isContestContinentalCup != 0) {
        _topOfStackMark = -1;
    }
}

/* IsContestContinentalCup: check if selectedContest is in continentalCups list.
 * continentalCups is a `CompetitionCup *[7]` table (8-byte pointers on
 * 64-bit). Walk it as `uintptr_t *` so each stride matches the actual
 * pointer width and we don't read half-pointers like the original
 * `uint32_t *` cast did. Sentinel value (`-1` cast) matches whatever
 * the Amiga binary terminated with. */
__attribute__((used, externally_visible))
void IsContestContinentalCup(void)
{
    uintptr_t *ptr = (uintptr_t *)_continentalCups;
    for (;;) {
        uintptr_t entry = *ptr++;
        if (entry == (uintptr_t)-1 || entry == 0)
            return;
        if (entry == _selectedContest) {
            _isContestContinentalCup = 1;
            return;
        }
    }
}

/* IsClubTeamsContest: check if contest is in nationalTeamCompetitionsAndQualifications.
 * Returns 0 if found (national team contest), 1 if not found (club teams contest).
 * contest is a Competition* pointer; pass as uintptr_t so 64-bit
 * pointers aren't truncated. The lookup table is similarly walked as
 * uintptr_t* — see IsContestContinentalCup for the rationale. */
__attribute__((used, externally_visible))
uint16_t IsClubTeamsContest(uintptr_t contest)
{
    uintptr_t *ptr = (uintptr_t *)_nationalTeamCompetitionsAndQualifications;
    while (*ptr != (uintptr_t)-1 && *ptr != 0) {
        if (*ptr == contest)
            return 0;
        ptr++;
    }
    return 1;
}

/* SetTeamsTypeForSelectedContest: set teamsType based on contest type */
__attribute__((used, externally_visible))
void SetTeamsTypeForSelectedContest(void)
{
    _teamsType = 1;
    if (IsClubTeamsContest(_selectedContest) == 0)
        _teamsType = 0;
}

/* ================================================================
 * Chunk 151: FormatPlayerValueOrStatusText
 *
 * in: a0 = team entry base (player data at +0x4C)
 *     a1 = destination buffer
 *
 * Checks player status byte at offset 0x71 from base:
 *   0x9B -> "TRIAL"
 *   0x9A -> "RES"
 * Then checks flag byte at offset 0x66, bit 1:
 *   set -> "LOAN"
 * Otherwise: format as monetary value (K or M).
 * ================================================================ */
extern const char _aTrial[];     /* "TRIAL" */
extern const char _aRes[];       /* "RES" */
extern const char _aLoan[];      /* "LOAN" */
extern const char _a01m[];       /* "\x9C%0.%1M" */
extern const char _a0m[];        /* "\x9C%0M" */
extern const char _a1k[];        /* "\x9C%1K" */

__attribute__((used, externally_visible))
void FormatPlayerValueOrStatusText(uint8_t *base, uint8_t *dst)
{
    /* Check status byte (someFlag in PlayerFile) */
    PlayerFile *pf = (PlayerFile *)(base + sizeof(TeamFile));
    if ((uint8_t)pf->form == PLAYER_FORM_TRIALIST) {
        StringCopy((const uint8_t *)_aTrial, dst);
        return;
    }
    if ((uint8_t)pf->form == PLAYER_FORM_RESERVE) {
        StringCopy((const uint8_t *)_aRes, dst);
        return;
    }
    /* Check loan flag (positionAndFace bit 1) */
    if (pf->positionAndFace & PF_LOAN) {
        StringCopy((const uint8_t *)_aLoan, dst);
        return;
    }

    /* Normal player: get displayed value, look up base price */
    uint16_t displayedVal = GetDisplayedPlayerValue(base);
    uint16_t price = _avgTeamValueBases[displayedVal];

    /* loc_127414: format value as currency string.
     * price is in thousands of pounds. Split into millions and sub-millions. */
    uint16_t millions = price / 1000;
    uint16_t subK = price % 1000;

    if (millions == 0) {
        /* Less than 1M: format as "XK" — %1=price */
        PrintFormatted((const uint8_t *)_a1k, dst, 0, price, 0, 0, 0, 0, 0, 0, 0, 0);
    } else if (subK == 0) {
        /* Exact millions: format as "XM" — %0=millions */
        PrintFormatted((const uint8_t *)_a0m, dst, millions, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    } else {
        /* Millions with decimal: check precision needed */
        uint16_t tens = subK / 10;      /* 2 most significant digits */
        uint16_t hundreds = tens / 10;   /* most significant digit */
        uint16_t tensRemainder = tens % 10;

        if (tensRemainder != 0) {
            /* Two decimal places: "X.YZM" — %0=millions, %1=tens */
            PrintFormatted((const uint8_t *)_a01m, dst, millions, tens, 0, 0, 0, 0, 0, 0, 0, 0);
        } else {
            /* One decimal place: "X.YM" — %0=millions, %1=hundreds */
            PrintFormatted((const uint8_t *)_a01m, dst, millions, hundreds, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }
}

/* ---- Chunk 219-220: ChooseTeams layout, team entries, selection ---- */

extern MenuEntry _menuEntries[];
extern uint16_t _chooseTeamsFriendly_leftListNumEntries;
extern uint16_t _chooseTeamsFriendly_middleListNumEntries;
extern uint16_t _chooseTeamsFriendly_rightListNumEntries;
extern uint16_t _chooseTeamsFriendly_menuTopY;
extern uint16_t _chooseTeamsFriendly_menuBottomY;
extern uint16_t _viewTeamsColumn1;
extern uint16_t _viewTeamsColumn2;
extern uint16_t _viewTeamsColumn3;
extern uint16_t _viewTeamsStartY;
extern uint16_t _viewTeamsAbortY;
extern uint16_t _selTeamsUpperBound;
extern uint8_t _teamNumbersIndices[];
extern uintptr_t _teamPointers[];
extern uint16_t _numSelectedTeamsCopy;
extern uint16_t _numNeededTeams;
extern uintptr_t _playWhatStr;
extern uint8_t _aChooseTeams[];
extern uint8_t _aChangeTeams[];
extern uint8_t _aSelect1MoreTea[];
extern uint8_t _aSelect0MoreTea[];
extern uint8_t _aSelect1LessTea[];
extern uint8_t _aSelect0LessTea[];
extern uint8_t _aError[];
extern uint8_t _aSelect1CpuTeam[];   /* "SELECT 1 COMPUTER TEAM" */
extern int16_t _chooseTeamsFriendly_changeTeamsPromptDrawList[];


__attribute__((used, externally_visible))
void ChooseTeamsFriendly_UpdateMenuEntryYLayout(void)
{
    uint16_t topY = _menuEntries[0].y + _menuEntries[0].dwHeight;
    _chooseTeamsFriendly_menuTopY = topY;
    uint16_t bottomY = _menuEntries[56].y;
    _chooseTeamsFriendly_menuBottomY = bottomY;

    /* d0 = max of left/middle/right list counts */
    uint16_t d0 = _chooseTeamsFriendly_leftListNumEntries;
    if (d0 < _chooseTeamsFriendly_middleListNumEntries)
        d0 = _chooseTeamsFriendly_middleListNumEntries;
    if (d0 < _chooseTeamsFriendly_rightListNumEntries)
        d0 = _chooseTeamsFriendly_rightListNumEntries;

    d0 *= 10;
    int16_t d1 = (int16_t)(bottomY - topY) - (int16_t)d0;
    d1 -= (int16_t)_menuEntries[1].dwHeight;
    /* ext.l d1; divu #3,d1 — unsigned division of sign-extended value */
    d1 = (int16_t)((uint32_t)(int32_t)d1 / 3);

    int16_t d2 = d1 + (int16_t)topY;

    /* Set y of entries 1..6 */
    MenuEntry *e = &_menuEntries[1];
    int16_t i;
    for (i = 0; i < 6; i++) {
        e->y = (uint16_t)d2;
        e++;
    }

    /* d2 += menuEntries[1].dwHeight, then d1 += d2 */
    d2 += (int16_t)_menuEntries[1].dwHeight;
    d1 += d2;

    /* 3 columns × 16 rows starting at entry 7 */
    e = &_menuEntries[7];
    int16_t col;
    for (col = 0; col < 3; col++) {
        int16_t yPos = d1;
        int16_t row;
        for (row = 0; row < 16; row++) {
            e->y = (uint16_t)yPos;
            yPos += 10;
            e++;
        }
    }
}

__attribute__((used, externally_visible))
void ViewSelTeamsSetColumns(void)
{
    /* d1 = max of column counts */
    uint16_t d1 = _viewTeamsColumn1;
    if (d1 < _viewTeamsColumn2)
        d1 = _viewTeamsColumn2;
    if (d1 < _viewTeamsColumn3)
        d1 = _viewTeamsColumn3;

    uint16_t startY = _menuEntries[0].y + _menuEntries[0].dwHeight;
    _viewTeamsStartY = startY;
    uint16_t abortY = _menuEntries[75].y;
    _viewTeamsAbortY = abortY;

    uint16_t headerHeight = _menuEntries[2].dwHeight;
    uint16_t d4 = 10;

    /* Find largest d4 where entries fit */
    for (;;) {
        uint16_t totalHeight = d1 * d4;
        int16_t remaining = (int16_t)(abortY - startY) - (int16_t)headerHeight - (int16_t)totalHeight;
        if (remaining >= 4)
            break;
        d4--;
    }

    int16_t space = (int16_t)(abortY - startY) - (int16_t)headerHeight - (int16_t)(d1 * d4);
    /* ext.l d1; divu #3,d1 */
    int16_t gap = (int16_t)((uint32_t)(int32_t)space / 3);

    int16_t d2 = gap + (int16_t)startY;

    /* Set y of entries 2..7 */
    MenuEntry *e = &_menuEntries[2];
    int16_t i;
    for (i = 0; i < 6; i++) {
        e->y = (uint16_t)d2;
        e++;
    }

    /* d2 += menuEntries[2].dwHeight, then gap += d2 */
    d2 += (int16_t)_menuEntries[2].dwHeight;
    gap += d2;

    /* 3 columns × 22 rows starting at entry 8 */
    e = &_menuEntries[8];
    int16_t col;
    for (col = 0; col < 3; col++) {
        int16_t yPos = gap;
        int16_t row;
        for (row = 0; row < 22; row++) {
            e->y = (uint16_t)yPos;
            e->dwHeight = d4 - 1;
            yPos += (int16_t)d4;
            e++;
        }
    }
}

/* ---- Chunk 220: SetTeamEntriesStringsAndColors, SetChooseChangeTeamsEntries,
       ChooseTeamsFriendly_DrawChangeTeamsPrompt, ChooseTeamsFriendlyCommon ---- */

#define TEAM_NOT_SELECTED 0

__attribute__((used, externally_visible))
void SetTeamEntriesStringsAndColors(void)
{
    uint8_t *indices = _teamNumbersIndices;
    MenuEntry *e = &_menuEntries[8];
    uint16_t d6 = 0;

    for (;;) {
        e->isInvisible = 1;

        if (d6 < _selTeamsUpperBound) {
            for (;;) {
                uint16_t ord = e->ordinal;
                ord -= 8;
                uint16_t col = ord / 22;
                uint16_t row = ord % 22;

                uint16_t colCount;
                if (col == 0)
                    colCount = _viewTeamsColumn1;
                else if (col == 1)
                    colCount = _viewTeamsColumn2;
                else
                    colCount = _viewTeamsColumn3;

                if (row < colCount) {
                    /* visible entry — set string and color */
                    e->isInvisible = 0;
                    int16_t idx = (int8_t)*indices++;
                    TeamFileEx *team = (TeamFileEx *)_teamPointers[idx];
                    e->fg = (uintptr_t)team->team.teamName;

                    uint16_t color = PINK_TO_BROWN_9;
                    uint8_t ctrl = team->team.teamControls;
                    if (ctrl != TEAM_NOT_SELECTED) {
                        color = RED_10;
                        if (ctrl != TEAM_COMPUTER) {
                            color = BLUE_TO_PURPLE_11;
                            if (ctrl != TEAM_PLAYER_COACH)
                                color = BRIGHT_BLUE_TO_BLUE_13;
                        }
                    }
                    e->bgColor = color;
                    break;
                }

                /* skip invisible entry, advance to next */
                e++;
                if (e->ordinal == 74)
                    return;
                e->isInvisible = 1;
                continue;
            }
        }

        e++;
        if (e->ordinal == 74)
            return;
        d6++;
    }
}

__attribute__((used, externally_visible))
void SetChooseChangeTeamsEntries(void)
{
    /* Entry 74 (0x1030/0x38) = "CHOOSE TEAMS" / "CHANGE TEAMS" label */
    MenuEntry *labelEntry = &_menuEntries[74];
    uint8_t *label = _aChooseTeams;
    if (_numSelectedTeamsCopy == _numNeededTeams)
        label = _aChangeTeams;
    labelEntry->fg = (uintptr_t)label;

    /* Entry 76 (0x10A0/0x38) = play prompt, Entry 77 (0x10D8/0x38) = select more/less */
    MenuEntry *playEntry = &_menuEntries[76];
    MenuEntry *selectEntry = &_menuEntries[77];

    /* SHOOTING PRACTICE: this is the screen where fire cycles a selected team
     * between COMPUTER, PLAYER-COACH and COACH, so it is the one most likely to
     * leave the drill with two human sides or two computer ones. Both teams
     * picked is not enough -- exactly one of them must be the computer
     * (shooting_practice.h). */
    int16_t practiceBlocked = g_shootingPracticeMode && !ShootingPractice_SelectionOk();

    if (_numSelectedTeamsCopy == _numNeededTeams && !practiceBlocked) {
        playEntry->isInvisible = 0;
        selectEntry->isInvisible = 1;
        playEntry->content = ENTRY_STRING;
        playEntry->fg = _playWhatStr;
    } else {
        playEntry->isInvisible = 1;
        selectEntry->isInvisible = 0;
    }

    uint8_t *dst = (uint8_t *)selectEntry->fg;
    int16_t diff = (int16_t)_numSelectedTeamsCopy - (int16_t)_numNeededTeams;
    if (diff == 0) {
        /* "ERROR" is the original's unreachable case; for the drill this is the
         * message actually on screen. */
        StringCopy(practiceBlocked ? _aSelect1CpuTeam : _aError, dst);
        return;
    }
    if (diff < 0) {
        diff = -diff;
        if (diff == 1) {
            StringCopy(_aSelect1MoreTea, dst);
            return;
        }
        PrintFormatted(_aSelect0MoreTea, dst, (uint32_t)diff, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    } else {
        if (diff == 1) {
            StringCopy(_aSelect1LessTea, dst);
            return;
        }
        PrintFormatted(_aSelect0LessTea, dst, (uint32_t)diff, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}

__attribute__((used, externally_visible))
void ChooseTeamsFriendly_DrawChangeTeamsPrompt(MenuEntry *savedA5)
{
    SetChooseChangeTeamsEntries();
    DrawMenuItemsWaitForVSync(_chooseTeamsFriendly_changeTeamsPromptDrawList);
}

__attribute__((used, externally_visible))
void ChooseTeamsFriendlyCommon(MenuEntry *entry)
{
    uint16_t d0 = entry->ordinal;
    d0 -= 8;
    /* Convert menu ordinal to linear team index across 3 columns */
    if (d0 >= 22) {
        if (d0 >= 44) {
            d0 -= 22;
            d0 += _viewTeamsColumn2;
        }
        d0 -= 22;
        d0 += _viewTeamsColumn1;
    }

    int8_t idx = (int8_t)_teamNumbersIndices[d0];
    int16_t idx16 = (int16_t)idx;
    TeamFileEx *team = (TeamFileEx *)_teamPointers[(uint16_t)idx16];
    uint8_t ctrl = team->team.teamControls;

    if (ctrl == TEAM_NOT_SELECTED) {
        entry->bgColor = RED_10;
        team->team.teamControls = TEAM_COMPUTER;
        _numSelectedTeamsCopy++;
    } else if (ctrl == TEAM_COMPUTER) {
        team->team.teamControls = TEAM_PLAYER_COACH;
        entry->bgColor = BLUE_TO_PURPLE_11;
    } else if (ctrl == TEAM_PLAYER_COACH) {
        team->team.teamControls = TEAM_COACH;
        entry->bgColor = BRIGHT_BLUE_TO_BLUE_13;
    } else {
        entry->bgColor = PINK_TO_BROWN_9;
        team->team.teamControls = TEAM_NOT_SELECTED;
        _numSelectedTeamsCopy--;
    }

    ChooseTeamsFriendly_DrawChangeTeamsPrompt(entry);
    CallDrawMenuItem(entry);
}

/* ---- Chunk 221: ChooseTeamsFriendlyCommon2 ---- */

extern uint8_t _teamsIndex[];
extern void *_teamsPtrTable[];
extern TeamFileEx *_selectedTeamPtr;
extern uint16_t _selectedTeamCountryNumber;
extern uintptr_t _chooseTeamsFunc;
extern uint16_t _careerSquadScreenDidSelectPlayerFlag;
extern uint16_t _abortSelectTeams;
extern uint16_t _chooseTeamsResult;
extern uint8_t _teamsTable[];
extern uint16_t _numSelectedTeams;

extern void Bureaucracy(void);
extern void SetExitMenuFlag(void);

__attribute__((used, externally_visible))
void ChooseTeamsFriendlyCommon2(MenuEntry *entry)
{
    uint16_t d0 = entry->ordinal;
    d0 -= 7;
    /* Convert menu ordinal to linear team index across 3 columns of 16 */
    if (d0 >= 16) {
        if (d0 >= 32) {
            d0 -= 16;
            d0 += _chooseTeamsFriendly_middleListNumEntries;
        }
        d0 -= 16;
        d0 += _chooseTeamsFriendly_leftListNumEntries;
    }

    int8_t idx = (int8_t)_teamsIndex[d0];
    int16_t idx16 = (int16_t)idx;
    TeamFileEx *team = (TeamFileEx *)_teamsPtrTable[(uint16_t)idx16];
    _selectedTeamPtr = team;
    _selectedTeamCountryNumber = BE_READ_U16(team);

    if (_chooseTeamsFunc != 0) {
        /* Asm: a0 = team pointer when jsr (a6) is called */
        void (*func)(uint8_t *) = (void (*)(uint8_t *))_chooseTeamsFunc;
        func((uint8_t *)team);
        if (_careerSquadScreenDidSelectPlayerFlag != 0) {
            /* The callback finished the job and wants to stay here — adding an
             * international player who went straight into a free squad slot.
             * Leaving the list up is what lets the next player be picked from
             * another club without walking the whole path again. Consume both
             * flags so the next club starts clean. */
            if (chooseTeamsKeepListOpen) {
                chooseTeamsKeepListOpen = 0;
                _careerSquadScreenDidSelectPlayerFlag = 0;
                return;
            }
            _abortSelectTeams = 1;
            _chooseTeamsResult = 0;
            SetExitMenuFlag();
            return;
        }
        return;
    }

    if (_careerSquadScreenDidSelectPlayerFlag != 0) {
        _abortSelectTeams = 1;
        _chooseTeamsResult = 0;
        SetExitMenuFlag();
        return;
    }

    uint8_t teamOrd = team->team.teamOrdinal;
    uint8_t *tableEntry = &_teamsTable[teamOrd];
    uint8_t ctrl = *tableEntry;

    if (ctrl == 0) {
        /* unselected → select as CPU */
        if (_numSelectedTeams >= 64)
            return;
        *tableEntry = 1;
        entry->bgColor = RED_10;
        TeamFileEx *copied = AddTeamToBuffer(team);
        copied->team.teamControls = 1;
    } else if (ctrl == 1) {
        /* CPU → player coach */
        *tableEntry = 2;
        TeamFileEx *found = FindTeam(team, (uint16_t *)0);
        found->team.teamControls = 2;
        entry->bgColor = BLUE_TO_PURPLE_11;
    } else if (ctrl == 2) {
        /* player coach → coach */
        *tableEntry = 3;
        TeamFileEx *found2 = FindTeam(team, (uint16_t *)0);
        found2->team.teamControls = 3;
        entry->bgColor = BRIGHT_BLUE_TO_BLUE_13;
    } else {
        /* coach → unselect */
        *tableEntry = 0;
        entry->bgColor = PINK_TO_BROWN_9;
        DeselectTeam(team);
    }

    Bureaucracy();
    CallDrawMenuItem(entry);
}

/* ---- Chunk 222: ChooseTeamsFriendlyInit ---- */

extern uint16_t _numSelectedTeams;
extern uint16_t _viewSelectedTeams;
extern uintptr_t _chooseTeamsHeader1stPart;
extern uintptr_t _gameTypeStr;
extern uint8_t *_curCountryStr;
extern uint16_t _choosingPreset;
extern uintptr_t _selectedContest;
extern uint8_t _copaAmerica[];
extern uint8_t _aAllSelectedTea[];

extern uint16_t GetStringLength(const uint8_t *str);
extern void SetCurrentEntry(uint16_t entryIndex);

__attribute__((used, externally_visible))
void ChooseTeamsFriendlyInit(void)
{
    _numSelectedTeamsCopy = _numSelectedTeams;
    _selTeamsUpperBound = _numSelectedTeams;

    /* Build teamPointers[] and teamNumbersIndices[] from selTeamsPtr */
    TeamFileEx *src = _selectedTeams;
    uint16_t i;
    for (i = 0; i < _selTeamsUpperBound; i++) {
        _teamPointers[i] = (uintptr_t)src;
        _teamNumbersIndices[i] = (uint8_t)i;
        src++;
    }

    /* Bubble sort teams alphabetically by name */
    if (_selTeamsUpperBound >= 2) {
        uint16_t n = _selTeamsUpperBound - 2;
        uint16_t swapped;
        do {
            swapped = 0;
            uint8_t *idx = _teamNumbersIndices;
            uint16_t j;
            for (j = 0; j <= n; j++) {
                int8_t i1 = (int8_t)idx[0];
                uint8_t *name1 = (uint8_t *)((TeamFileEx *)_teamPointers[(uint16_t)(int16_t)i1])->team.teamName;
                int8_t i2 = (int8_t)idx[1];
                uint8_t *name2 = (uint8_t *)((TeamFileEx *)_teamPointers[(uint16_t)(int16_t)i2])->team.teamName;

                uint8_t *p1 = name1;
                uint8_t *p2 = name2;
                for (;;) {
                    uint8_t c = *p1++;
                    if (c == 0)
                        break;
                    uint8_t c2 = *p2++;
                    if (c < c2)
                        break;
                    if (c > c2) {
                        /* swap indices */
                        uint8_t tmp = idx[0];
                        idx[0] = idx[1];
                        idx[1] = tmp;
                        swapped = 1;
                        break;
                    }
                }
                idx++;
            }
        } while (swapped);
    }

    /* Build header string */
    uint8_t *headerDst = (uint8_t *)_menuEntries[0].fg;
    uint8_t *headerPrefix = (uint8_t *)_chooseTeamsHeader1stPart;
    if (headerPrefix != (uint8_t *)0) {
        StringCopy(headerPrefix, headerDst);
    } else {
        headerDst = StringCopy(_aAllSelectedTea, headerDst);
        uint8_t *gts = (uint8_t *)_gameTypeStr;
        if (gts != (uint8_t *)0) {
            headerDst = StringCopy(gts, headerDst);
        }
        /* Check if country name fits */
        uint16_t hdrLen = GetStringLength((const uint8_t *)_menuEntries[0].fg);
        uint16_t countryLen = GetStringLength((const uint8_t *)_curCountryStr);
        if (hdrLen + countryLen <= 47) {
            *headerDst++ = ' ';
            *headerDst++ = '-';
            *headerDst++ = ' ';
            StringCopy((const uint8_t *)_curCountryStr, headerDst);
        }
    }

    /* Calculate column counts */
    if (_selTeamsUpperBound <= 8) {
        _viewTeamsColumn1 = 0;
        _viewTeamsColumn2 = _selTeamsUpperBound;
        _viewTeamsColumn3 = 0;
    } else {
        uint16_t base = _selTeamsUpperBound / 3;
        uint16_t rem = _selTeamsUpperBound % 3;
        _viewTeamsColumn1 = base;
        _viewTeamsColumn2 = base;
        _viewTeamsColumn3 = base;
        if (rem == 1) {
            _viewTeamsColumn2++;
        } else if (rem == 2) {
            _viewTeamsColumn1++;
            _viewTeamsColumn2++;
        }
    }

    ViewSelTeamsSetColumns();

    /* Handle choosingPreset / copaAmerica */
    if (_choosingPreset != 0) {
        if (_selectedContest != 0 &&
            comp_id((void *)_selectedContest) == COMPID_copaAmerica) {
            _menuEntries[74].isInvisible = 1;
        }
    }

    SetChooseChangeTeamsEntries();
    SetTeamEntriesStringsAndColors();

    if (_viewSelectedTeams == 2) {
        SetCurrentEntry(76);
    } else if (_viewSelectedTeams == 1) {
        SetCurrentEntry(74);
    }
}

/* ---- Chunk 225: SelTeamForTacticsInit ---- */

extern uintptr_t _chooseTeamMenuName;
extern uint16_t _numTeamsCountryBuffer;
extern uint16_t _selTeamsColumn1;
extern uint16_t _selTeamsColumn2;
extern uint16_t _selTeamsColumn3;
extern uint8_t _indexCountries[];
extern void *_dwArrayCountries[];

extern void SelTeamForTactics_ReflowTeamListLayout(void);

__attribute__((used, externally_visible))
void SelTeamForTacticsInit(void)
{
    _menuEntries[0].fg = _chooseTeamMenuName;

    /* Calculate column counts from numTeamsCountryBuffer */
    if (_numTeamsCountryBuffer >= 66) {
        _selTeamsColumn1 = 22;
        _selTeamsColumn2 = 21;
        _selTeamsColumn3 = 21;
    } else if (_numTeamsCountryBuffer <= 8) {
        _selTeamsColumn1 = 0;
        _selTeamsColumn2 = _numTeamsCountryBuffer;
        _selTeamsColumn3 = 0;
    } else {
        uint16_t base = _numTeamsCountryBuffer / 3;
        uint16_t rem = _numTeamsCountryBuffer % 3;
        _selTeamsColumn1 = base;
        _selTeamsColumn2 = base;
        _selTeamsColumn3 = base;
        if (rem == 1) {
            if (_numTeamsCountryBuffer == 64)
                _selTeamsColumn1++;
            else
                _selTeamsColumn2++;
        } else if (rem == 2) {
            _selTeamsColumn1++;
            _selTeamsColumn2++;
        }
    }

    SelTeamForTactics_ReflowTeamListLayout();
    /* bra.w *+4 = NOP */

    /* Build menu entries from indexCountries/dwArrayCountries */
    uint8_t *indices = _indexCountries;
    MenuEntry *e = &_menuEntries[1]; /* 0x38/56=1 */
    uint16_t d6 = 0;

    for (;;) {
        e->isInvisible = 1;

        if (d6 < _numTeamsCountryBuffer) {
            for (;;) {
                uint16_t ord = e->ordinal;
                ord -= 1;
                uint16_t col = ord / 22;
                uint16_t row = ord % 22;

                uint16_t colCount;
                if (col == 0)
                    colCount = _selTeamsColumn1;
                else if (col == 1)
                    colCount = _selTeamsColumn2;
                else
                    colCount = _selTeamsColumn3;

                if (row < colCount) {
                    /* Visible — set team name pointer */
                    e->isInvisible = 0;
                    int8_t idx = (int8_t)*indices++;
                    e->fg = (uintptr_t)_dwArrayCountries[(uint16_t)(int16_t)idx];
                    break;
                }

                e++;
                if (e->ordinal == 67)
                    return;
                e->isInvisible = 1;
            }
        }

        e++;
        if (e->ordinal == 67)
            return;
        d6++;
    }
}

/* ---- Chunk 223: ChooseTeamsFriendlyInitFinish ---- */

extern int16_t _topOfStackMark;
extern uint16_t _selectTeamsNoLimit;
extern uint8_t _aAbort[];
extern uint16_t _inFriendlyMenu;
extern uint16_t _leagueNumber;
extern uint16_t _chooseTeamsFriendly_numTeamsInLeagueList;
extern uint8_t _lastLoadedExtNumber;
extern uint8_t _teamFileBuffer[];

__attribute__((used, externally_visible))
void ChooseTeamsFriendlyInitFinish(void)
{
    /* If topOfStackMark < 0, set abort button */
    if ((int16_t)_topOfStackMark < 0) {
        MenuEntry *abortEntry = &_menuEntries[56]; /* 0xC40/56=56 */
        abortEntry->fg = (uintptr_t)_aAbort;
        uint16_t bgc = abortEntry->bgColor;
        bgc = (bgc & 0xFFF0) + RED_10;
        abortEntry->bgColor = bgc;
    }

    /* If selectTeamsNoLimit, hide entries 1-6 */
    if (_selectTeamsNoLimit != 0) {
        _menuEntries[1].isInvisible = 1;
        _menuEntries[2].isInvisible = 1;
        _menuEntries[3].isInvisible = 1;
        _menuEntries[4].isInvisible = 1;
        _menuEntries[5].isInvisible = 1;
        _menuEntries[6].isInvisible = 1;
    }

    /* Build teamsIndex[] and teamsPtrTable[] from teamFileBuffer */
    uint8_t *idx = _teamsIndex;
    void   **ptrs = _teamsPtrTable;
    uint16_t numTeamsInFile = *(uint16_t *)_teamFileBuffer;
    TeamFileEx *teamRecord = (TeamFileEx *)(_teamFileBuffer + 2);
    uint8_t league = (uint8_t)_leagueNumber;
    uint16_t count = 0;
    uint16_t selNum = _selTeamNumber;
    uint16_t i;

    for (i = 0; i < numTeamsInFile; i++) {
        if (teamRecord->team.league == league) {
            /* selNum is BE-packed (country<<8 | ordinal); compare via
             * PACK_BE_U16 so the user's own team is correctly skipped
             * out of the friendly-menu list on LE hosts (a raw
             * *(uint16_t *)teamRecord would give ordinal<<8|country
             * and the skip would never fire). */
            if (_inFriendlyMenu != 0 ||
                PACK_BE_U16(teamRecord->team.countryNumber,
                            teamRecord->team.teamOrdinal) != selNum) {
                *idx++ = (uint8_t)count;
                *ptrs++ = teamRecord;
                count++;
            }
        }
        teamRecord++;
    }
    _chooseTeamsFriendly_numTeamsInLeagueList = count;

    /* Bubble sort teamsIndex by team name */
    if (count >= 2) {
        uint16_t n = count - 2;
        uint16_t swapped;
        do {
            swapped = 0;
            uint8_t *si = _teamsIndex;
            uint16_t j;
            for (j = 0; j <= n; j++) {
                int8_t i1 = (int8_t)si[0];
                uint8_t *name1 = (uint8_t *)((TeamFileEx *)_teamsPtrTable[(uint16_t)(int16_t)i1])->team.teamName;
                int8_t i2 = (int8_t)si[1];
                uint8_t *name2 = (uint8_t *)((TeamFileEx *)_teamsPtrTable[(uint16_t)(int16_t)i2])->team.teamName;

                uint8_t *p1 = name1;
                uint8_t *p2 = name2;
                for (;;) {
                    uint8_t c = *p1++;
                    if (c == 0) break;
                    uint8_t c2 = *p2++;
                    if (c < c2) break;
                    if (c > c2) {
                        uint8_t tmp = si[0];
                        si[0] = si[1];
                        si[1] = tmp;
                        swapped = 1;
                        break;
                    }
                }
                si++;
            }
        } while (swapped);
    }

    /* Clear teamsTable (100 bytes) — use volatile to prevent memset optimization */
    {
        volatile uint8_t *tt = (volatile uint8_t *)_teamsTable;
        for (i = 0; i < 100; i++)
            tt[i] = 0;
    }

    /* Mark previously selected teams in teamsTable */
    if (_numNeededTeams != 1 && _numSelectedTeams != 0) {
        uint8_t *selTeam = &_selTeamsPtr;
        uint8_t extNum = _lastLoadedExtNumber;
        uint8_t lg = (uint8_t)_leagueNumber;
        uint16_t ns;

        for (ns = 0; ns < _numSelectedTeams; ns++) {
            if (_lastLoadedExtNumber == 0x64 ||
                (*(selTeam) == extNum && *(selTeam + 0x19) == lg)) {
                uint8_t ordinal = *(selTeam + 1);
                _teamsTable[ordinal] = *(selTeam + 4);
            }
            selTeam += 684;
        }
    }

    /* Calculate column counts */
    if (_chooseTeamsFriendly_numTeamsInLeagueList >= 48) {
        _chooseTeamsFriendly_leftListNumEntries = 16;
        _chooseTeamsFriendly_middleListNumEntries = 16;
        _chooseTeamsFriendly_rightListNumEntries = 16;
    } else if (_chooseTeamsFriendly_numTeamsInLeagueList <= 8) {
        _chooseTeamsFriendly_leftListNumEntries = 0;
        _chooseTeamsFriendly_middleListNumEntries = _chooseTeamsFriendly_numTeamsInLeagueList;
        _chooseTeamsFriendly_rightListNumEntries = 0;
    } else {
        uint16_t base = _chooseTeamsFriendly_numTeamsInLeagueList / 3;
        uint16_t rem = _chooseTeamsFriendly_numTeamsInLeagueList % 3;
        _chooseTeamsFriendly_leftListNumEntries = base;
        _chooseTeamsFriendly_middleListNumEntries = base;
        _chooseTeamsFriendly_rightListNumEntries = base;
        if (rem == 1) {
            _chooseTeamsFriendly_middleListNumEntries++;
        } else if (rem == 2) {
            _chooseTeamsFriendly_leftListNumEntries++;
            _chooseTeamsFriendly_middleListNumEntries++;
        }
    }

    ChooseTeamsFriendly_UpdateMenuEntryYLayout();
    ChooseTeamsSetHeader();
    CheckNumTeamsAllowed();
    /* bra.w *+4 = NOP (fall through) */

    /* Build menu entries from teamsIndex/teamsPtrTable */
    MenuEntry *e = &_menuEntries[7]; /* entry 7 = 0x188/0x38 */
    uint16_t d6 = 0;
    uint16_t d5 = 0;

outer:
    e->isInvisible = 1;

    /* Find next visible entry slot (row < column count) */
    for (;;) {
        uint16_t ord = e->ordinal;
        ord -= 7;
        uint16_t col = ord / 16;
        uint16_t row = ord % 16;

        uint16_t colCount;
        if (col == 0)
            colCount = _chooseTeamsFriendly_leftListNumEntries;
        else if (col == 1)
            colCount = _chooseTeamsFriendly_middleListNumEntries;
        else
            colCount = _chooseTeamsFriendly_rightListNumEntries;

        if (row < colCount)
            break;

        e++;
        if (e->ordinal == 55)
            return;
        e->isInvisible = 1;
    }

    /* Entry is visible — populate it */
    uint16_t defaultColor = _selectTeamsNoLimit ? 5 : 9;
    e->isInvisible = 0;

    int8_t tidx = (int8_t)_teamsIndex[d5];
    TeamFileEx *team = (TeamFileEx *)_teamsPtrTable[(uint16_t)(int16_t)tidx];
    uint8_t teamOrd = team->team.teamOrdinal;
    e->fg = (uintptr_t)team->team.teamName;

    uint16_t color = defaultColor;
    uint8_t ctrl = _teamsTable[teamOrd];
    if (ctrl != 0) {
        color = 10;
        if (ctrl != 1) {
            color = 11;
            if (ctrl != 2)
                color = 13;
        }
    }
    e->bgColor = color;

    /* Advance: mark remaining entries invisible, increment d5 if more teams */
    for (;;) {
        e++;
        if (e->ordinal == 55)
            return;
        e->isInvisible = 1;
        d6++;
        if (d6 < _chooseTeamsFriendly_numTeamsInLeagueList) {
            d5++;
            goto outer;
        }
        /* d6 >= count: continue marking invisible */
    }
}

/* ---- Chunk 224: SetTeamsCoordinates, SelectTeamsReinit, SelectTeamsInit ---- */

extern uint16_t _numTeamsColumn1;
extern uint16_t _numTeamsColumn2;
extern uint16_t _numTeamsColumn3;
extern uint16_t _numLeaguesInCountry;
extern uint16_t _chooseTeamsNumTeams;
extern uint16_t _headerYPlusHeight;
extern uint16_t _exitY;
extern uint16_t _teamDepthLevel;
extern uint16_t _startingTeamIndex;
extern uint16_t _teamsLowerBound;
extern uint16_t _prevSelectedTeamEntry;
extern uint16_t _stayInSelectTeams;
extern uint16_t _exitFlag;
extern uint8_t _aChooseTeamsFor[];
extern uint8_t _aChangeTeamsFor[];

extern void SetLeagueNames(void);
extern void SetCountryNames(void);
extern void CheckNumberOfTeams(void);
extern void ExitSelectTeams(void);
extern MenuEntry *CalcMenuEntryAddress(uint16_t ordinal);
extern void SelectLeaguesCommon(MenuEntry *entry);

__attribute__((used, externally_visible))
void SetTeamsCoordinates(void)
{
    /* d1 = max of column counts */
    uint16_t d1 = _numTeamsColumn1;
    if (d1 < _numTeamsColumn2) d1 = _numTeamsColumn2;
    if (d1 < _numTeamsColumn3) d1 = _numTeamsColumn3;

    d1 += _numLeaguesInCountry;

    uint16_t topY = _menuEntries[0].y + _menuEntries[0].dwHeight;
    _headerYPlusHeight = topY;
    _exitY = _menuEntries[88].y; /* 0x1340/56=88 */

    uint16_t totalHeight = d1 * 9;
    int16_t space = (int16_t)(_exitY - _headerYPlusHeight) - (int16_t)totalHeight;
    int16_t yPos = (space >> 1) + (int16_t)_headerYPlusHeight;

    /* Set league entry Y coordinates */
    if (_numLeaguesInCountry != 0) {
        /* tst.w chooseTeamsNumTeams is a dead check (followed by unconditional lea) */
        MenuEntry *e = &_menuEntries[11]; /* 0x268/56=11 */
        int16_t n = (int16_t)_numLeaguesInCountry - 1;
        int16_t j;
        for (j = 0; j <= n; j++) {
            e->y = (uint16_t)yPos;
            yPos += 9;
            e++;
        }
        /* Note: unreachable code at 55337-55348 in asm (after bra.s) — skipped */
    }

    /* Set team entry Y coordinates: 3 columns × 22 entries starting at entry 21 */
    yPos += 2;
    MenuEntry *e = &_menuEntries[21]; /* 0x498/56=21 */
    int16_t col;
    for (col = 0; col < 3; col++) {
        int16_t y = yPos;
        int16_t row;
        for (row = 0; row < 22; row++) {
            e->y = (uint16_t)y;
            y += 9;
            e++;
        }
    }
}

__attribute__((used, externally_visible))
void SelectTeamsReinit(void)
{
    /* Adjust Y of entries 87-90 if too many teams+leagues */
    uint16_t d0 = _chooseTeamsNumTeams;
    d0 += 2;
    d0 = d0 / 3;
    d0 += _numLeaguesInCountry;
    if (d0 > 18) {
        int16_t yAdj = 10;
        if (d0 != 19)
            yAdj = 15;
        _menuEntries[87].y += yAdj; /* 0x1308/56=87 */
        _menuEntries[88].y += yAdj; /* 0x1340/56=88 */
        _menuEntries[89].y += yAdj; /* 0x1378/56=89 */
        _menuEntries[90].y += yAdj; /* 0x13B0/56=90 */
    }

    /* If at top of stack, show "ABORT" button */
    if (_teamDepthLevel == (uint16_t)_topOfStackMark) {
        MenuEntry *abortEntry = &_menuEntries[88]; /* 0x1340/56=88 */
        abortEntry->fg = (uintptr_t)_aAbort;
        uint16_t bgc = abortEntry->bgColor;
        bgc = (bgc & 0xFFF0) + RED_10;
        abortEntry->bgColor = bgc;
    }

    /* Build header string */
    uint8_t *headerDst = (uint8_t *)_menuEntries[0].fg;
    uint8_t *prefix = (uint8_t *)_chooseTeamsHeader1stPart;
    if (prefix != (uint8_t *)0) {
        headerDst = StringCopy(prefix, headerDst);
    } else {
        uint8_t *label = _aChooseTeamsFor;
        if (_numSelectedTeams == _numNeededTeams)
            label = _aChangeTeamsFor;
        headerDst = StringCopy(label, headerDst);
    }
    uint8_t *gts = (uint8_t *)_gameTypeStr;
    if (gts != (uint8_t *)0) {
        headerDst = StringCopy(gts, headerDst);
    }

    /* Check if country name fits (total <= 47) */
    uint16_t hdrLen = GetStringLength((const uint8_t *)_menuEntries[0].fg);
    uint16_t countryLen = GetStringLength((const uint8_t *)_curCountryStr);
    if (hdrLen + countryLen <= 47) {
        *headerDst++ = ' ';
        *headerDst++ = '-';
        *headerDst++ = ' ';
        StringCopy((const uint8_t *)_curCountryStr, headerDst);
    }

    /* Initialize team display parameters */
    _startingTeamIndex = 0;
    _teamsLowerBound = 0;

    /* Calculate column counts */
    if (_chooseTeamsNumTeams >= 66) {
        _numTeamsColumn1 = 22;
        _numTeamsColumn2 = 21;
        _numTeamsColumn3 = 21;
    } else if (_chooseTeamsNumTeams <= 8) {
        _numTeamsColumn1 = 0;
        _numTeamsColumn2 = _chooseTeamsNumTeams;
        _numTeamsColumn3 = 0;
    } else {
        uint16_t base = _chooseTeamsNumTeams / 3;
        uint16_t rem = _chooseTeamsNumTeams % 3;
        _numTeamsColumn1 = base;
        _numTeamsColumn2 = base;
        _numTeamsColumn3 = base;
        if (rem == 1) {
            if (_chooseTeamsNumTeams == 64)
                _numTeamsColumn1++;
            else
                _numTeamsColumn2++;
        } else if (rem == 2) {
            _numTeamsColumn1++;
            _numTeamsColumn2++;
        }
    }

    SetTeamsCoordinates();
    SetLeagueNames();
    SetCountryNames();

    /* Set cursor to previous selection if valid */
    uint16_t prevEntry = _prevSelectedTeamEntry;
    if (prevEntry != 0xFFFF) {
        MenuEntry *e = CalcMenuEntryAddress(prevEntry);
        if (e->isInvisible != 0) {
            prevEntry = 0xFFFF;
        } else {
            SetCurrentEntry(prevEntry);
            goto doCheckTeams;
        }
    }
    if (_chooseTeamsNumTeams == 0) {
        SetCurrentEntry(11);
    }
doCheckTeams:
    CheckNumberOfTeams();
}

/* ---- Chunk 226: ChooseTeamsInit ---- */
extern uint16_t _showClubTeams;
extern uint16_t _showNationalTeams;
extern uint16_t _showOrigCustomTeams;

__attribute__((used, externally_visible))
void ChooseTeamsInit(void)
{
    /* d7 = half the vertical gap between entry 0 and entry 1 */
    int16_t d7 = (int16_t)(_menuEntries[1].y - _menuEntries[0].y) >> 1;
    int16_t d6 = 4; /* count of visible categories */

    if (_showClubTeams == 0) {
        d6--;
        _menuEntries[0].isInvisible = 1;
        _menuEntries[1].y -= d7;
        _menuEntries[2].y -= d7;
        _menuEntries[3].y -= d7;
        SetCurrentEntry(1);
    }

    if (_showNationalTeams == 0) {
        d6--;
        _menuEntries[1].isInvisible = 1;
        _menuEntries[0].y += d7;
        _menuEntries[2].y -= d7;
        _menuEntries[3].y -= d7;
        if (_showClubTeams == 0)
            SetCurrentEntry(2);
    }

    if (_showOrigCustomTeams == 0) {
        d6 -= 2;
        _menuEntries[2].isInvisible = 1;
        _menuEntries[3].isInvisible = 1;
        _menuEntries[0].y += d7;
        _menuEntries[0].y += d7; /* asm adds d7 to y twice (offset 0x16) */
        _menuEntries[1].y += d7;
        _menuEntries[1].y += d7; /* same: y += 2*d7 total */
    }

    if (d6 == 1) {
        if (_showClubTeams != 0) {
            ClubTeams();
        } else {
            NationalTeams();
        }
        return;
    }

    /* entry 5 = (menuEntries+MENUENTRY_ALREADYDRAWN+$118) = entry at offset 0x118/56 = 5 */
    MenuEntry *headerEntry = &_menuEntries[5];
    char *dst = (char *)headerEntry->fg;
    char *src = (char *)_chooseTeamsHeader1stPart;
    if (src != 0) {
        dst = (char *)StringCopy((const uint8_t *)src, (uint8_t *)dst);
    } else {
        dst = (char *)StringCopy(_aChooseTeamsFor, (uint8_t *)dst);
    }
    src = (char *)_gameTypeStr;
    if (src != 0) {
        StringCopy((const uint8_t *)src, (uint8_t *)dst);
    }
}

__attribute__((used, externally_visible))
void SelectTeamsInit(void)
{
    SelectTeamsReinit();
    if (_stayInSelectTeams != 0) {
        SelectLeaguesCommon(&_menuEntries[11]); /* entry 11 = 0x268/56 */
    } else {
        if (_exitFlag != 0)
            ExitSelectTeams();
    }
}

/* ================================================================
 * SelectTeamsFinalMenu  (SWOS2.s line 51856)
 *
 * Main team selection state machine. Enumerates competitions for
 * a given country, builds team/league name lists, shows the
 * selectTeamsMenu, and handles navigation, team loading, and
 * competition setup.
 *
 * Asm register convention:
 *   in:  d0.b = mode (0xFF=default, or a nationality/country number)
 * ================================================================ */
__attribute__((used, externally_visible))
void SelectTeamsFinalMenu(uint16_t mode)
{
    uint16_t d6;            /* country/team-file number, preserved across calls */
    uint16_t d7;            /* current country index byte */
    uint16_t d1;            /* counter for enumeration */
    /* The competition list is a `Competition *[N]` terminated by
     * (Competition *)-1 / -2 sentinels — entries are native pointers
     * (4B on 32-bit Amiga, 8B on 64-bit). Use void** so iteration
     * advances by sizeof(void*) per entry on both targets, and the
     * sentinel comparisons stay correct (low-32-bit truncation of -1
     * is still 0xFFFFFFFF). */
    void   **a5;
    uint8_t *compEntry;     /* a1 - current competition entry pointer */
    void    *compListAddr;
    void   **compList;
    void   **teamNamesPtr;
    void   **leaguesPtrOut;
    uint16_t *leagueNumsOut;
    void   **countryNamesPtr;
    uint16_t cnCount;
    uint8_t *contest;
    uint8_t *playRec;       /* the record actually played: a preset euro cup is
                             * substituted by its 36-team tournament record */
    uint16_t lnum;
    uint16_t countryForName;
    uint16_t countryIdx;
    uint8_t *ctEntry;
    uint8_t compType;
    uint16_t divIdx;
    uint8_t nameOff;
    uint8_t *namePtr;
    int16_t off16;
    uint8_t *cPtr;
    uint8_t cByte;
    uint16_t cidx;
    uint16_t countryByte;

    _leagueNumber = 0;
    HLOG("SelectTeamsFinalMenu enter mode=%u\n", (unsigned)mode);

    /* Initialize customsLeague: offset 9 = comp.divisionsCount, offset 0xD = divisions[0].teams */
    _customsLeague[9] = 3;
    _customsLeague[0xD] = 20;

    if (_teamsType == EDITED_CUSTOM_TEAMS) {
        if (Asset_PromptInsertSaveDiskDf0(_aEditedCustomTe_0) != 0)
            goto exit_clear;
        if (LoadTeamFile(100) != 0)
            goto exit_clear;
        if (*(uint16_t *)_teamFileBuffer == 0x30) {
            _customsLeague[9] = 1;
            _customsLeague[0xD] = 0x30;
        }
    }

    /* loc_13DF8E: always executed (unconditional in asm) */
    _customsLeague[9] = 1;
    _customsLeague[0xD] = 0x30;

    _exitSelectTeams = 0;
    _isCountryNumber = 0;
    _isContestContinentalCup = 0;

    if ((uint8_t)mode != 255) {
        d6 = (uint8_t)mode;
        d7 = (uint8_t)mode;
        _teamDepthLevel = 2;
        _topOfStackMark = 2;
        goto loc_E004;
    }

    /* loc_13DFD4: default mode */
    d6 = 254;
    if (_teamsType == EDITED_CUSTOM_TEAMS) {
        d6 = 100;
    } else if (_teamsType == ORIGINAL_CUSTOM_TEAMS) {
        d6 = 72;
    }

    /* loc_13DFF6: */
    d7 = (uint8_t)d6;
    _topOfStackMark = 0;
    _teamDepthLevel = 0;

loc_E004:
    SetSelectedTeamsStackTop();
    _countryStackPtr = _countriesStack;

loc_E012:
    /* Main loop: enumerate competitions for country d7 */
    d7 &= 0xFF;
    countryForName = d7;
    if (_isCountryNumber != 0)
        countryForName = d6;
    GetCountryName(countryForName);

    _gotCountryName = 0;
    if (_teamDepthLevel == 2 && _isCountryNumber == 0)
        _gotCountryName = 1;

    /* loc_13E046: enumerate competitions */
    _numberOfLeagues = 0;
    compListAddr = _competitionsTable[d7];
    if (compListAddr == 0) {
        _panicReasonCode = 0x8632;
        PanicHaltLoop();
    }
    compList = (void **)compListAddr;
    teamNamesPtr = _teamNames;
    leaguesPtrOut = _leaguesPtrTable;
    leagueNumsOut = _leagueNumbersTable;
    d1 = 0;
    a5 = compList;

    /* loc_13E086: iterate through competition entries */
loc_E086:
    compEntry = (uint8_t *)(*a5);
    a5++;
    HLOG("  walk d7=%u comp=%p\n", (unsigned)(d7 & 0xFF), (void *)compEntry);

    if (compEntry == (uint8_t *)(uintptr_t)-1)
        goto loc_E2D0;

    if (compEntry == (uint8_t *)(uintptr_t)-2) {
        if (_showCupsAndOther == 0)
            goto loc_E2D0;
        goto loc_E086;
    }

    /* Career mode continental cup filtering (loc_13E0A6) */
    if (_gameType == GAME_TYPE_CAREER) {
        /* NOTE: Copa Libertadores is deliberately NOT filtered here. It is a
         * CLUB cup — the CONMEBOL analog of the European club cups
         * (euroCup/cupWinnersCup/uefaCup), which are likewise absent from this
         * filter and so always render in the EUROPE region view. Copa is routed
         * into the quaternary slot for a CONMEBOL career exactly as the euro
         * cups are (career_copa.c, "slot-3 mirrors euro"), and
         * Career_ViewOrEditCompetitionSlot handles it identically (found_slot →
         * cup view when it's your quaternary comp, auto-advance otherwise —
         * same as a euro cup browsed by a non-European career). An earlier
         * `goto loc_E086` hid it from the career VIEW WORLD/South America list,
         * leaving the green competition box empty where the euro cups fill it
         * for Europe. Removing the skip restores the symmetric behaviour.
         * (The national-team continental cups below stay region-gated — those
         * are national comps, not club cups.) */
        if (compEntry == _africanNationsCup) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_AFRICA)
                goto loc_E086;
            goto loc_E114;
        }
        if (compEntry == _copaAmerica) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_SOUTH_AMERICA)
                goto loc_E086;
            goto loc_E114;
        }
        if (compEntry == _northAmericanCup) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_NORTH_AMERICA)
                goto loc_E086;
            goto loc_E114;
        }
        if (compEntry == _asianCup) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_ASIA)
                goto loc_E086;
            goto loc_E114;
        }
        if (compEntry == _oceaniaCup) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_OCEANIA)
                goto loc_E086;
            goto loc_E114;
        }
    }

    /* loc_13E120: European Championships check */
    if (compEntry == _europeanChampionships) {
        if (_gameType == GAME_TYPE_CAREER) {
            if ((uint8_t)(_selTeamNumber >> 8) != CN_EUROPE)
                goto loc_E086;
            if (!(_someTeamFlags & (1 << 0))) {
                if (!(_someTeamFlags & (1 << 1)))
                    goto loc_E086;
            }
        }
    }

    goto skip_continental_check;
loc_E114:
    if (!(_someTeamFlags & (1 << 1)))
        goto loc_E086;
skip_continental_check:

    /* loc_13E154: World Cup qualification checks */
    if ((uintptr_t)compEntry == 252 ||
        compEntry == _northAmericaWorldCupQualification ||
        compEntry == _southAmericaWorldCupQualification ||
        compEntry == _africaWorldCupQualification ||
        compEntry == _asiaWorldCupQualification ||
        compEntry == _oceaniaWorldCupQualification) {
        /* loc_13E184: */
        if (_gameType != GAME_TYPE_CAREER)
            goto loc_E086;
        if (_isNationalSelector == 0)
            goto loc_E086;
        if (!(_someTeamFlags & (1 << 6)))
            goto loc_E086;
    }

    /* loc_13E1A6: World Cup check */
    if (compEntry == _worldCup) {
        if (_gameType == GAME_TYPE_CAREER) {
            if (_isNationalSelector == 0)
                goto loc_E086;
            if (!(_someTeamFlags & (1 << 7)))
                goto loc_E086;
        }
    }

    /* loc_13E1CE: depth level filtering */
    if (_showCupsAndOther == 0 &&
        _teamsType != ORIGINAL_CUSTOM_TEAMS &&
        _teamsType != EDITED_CUSTOM_TEAMS &&
        _teamDepthLevel != 2) {
        goto loc_E086;
    }

    /* loc_13E1F6: entry type dispatch */
    if ((uintptr_t)compEntry < 256) {
        /* Country entry: get country name from countriesTable */
        countryIdx = (uint16_t)(uintptr_t)compEntry;
        ctEntry = (uint8_t *)_countriesTable[countryIdx];
        *teamNamesPtr++ = (void *)(ctEntry + 1);
        *leaguesPtrOut++ = compEntry;
        *leagueNumsOut++ = 0xFFFF;
        d1++;
        goto loc_E086;
    }

    /* loc_13E21E: competition entry (>= 256) */
    {
        Competition       *compHdr = (Competition *)compEntry;
        CompLeagueHeader *league  = (CompLeagueHeader *)compEntry;
        compType = compHdr->type;
    if (compType == 0) {
        /* League: enumerate divisions.
         *
         * The Competition's embedded name table is 2 char* entries per
         * division (full name + abbreviation, e.g. "PREMIER LEAGUE" +
         * "PREMIER"). Amiga stride was 2*4 = 8 bytes per division and
         * we read a single 32-bit pointer at the start. On 64-bit each
         * char* is 8 bytes so the stride is 16 and we must read 8
         * bytes — use typed `char **` indexing so the layout is
         * pointer-width-correct on both targets. */
        divIdx = 0;
loc_E246:
        {
            /* leagues: 2 char* per division (full name + abbreviation) */
            char **nameTbl = comp_name_table(compHdr);
            namePtr = nameTbl ? (uint8_t *)nameTbl[divIdx * 2]
                              : (uint8_t *)_divisionStringTable[divIdx];
        }
        *teamNamesPtr++ = (void *)namePtr;
        *leaguesPtrOut++ = compEntry;
        *leagueNumsOut++ = divIdx;
        d1++;
        divIdx++;
        HLOG("    div %u/%u (comp=%p)\n", (unsigned)divIdx,
             (unsigned)(uint8_t)league->divisionsCount, (void *)compEntry);
        if ((uint8_t)divIdx != (uint8_t)league->divisionsCount)
            goto loc_E246;
        _numberOfLeagues = (int8_t)league->divisionsCount;
        goto loc_E086;
    } else if (compType == 1) {
        /* Cup — single name pointer at +5+off16 (1 entry, char *). */
        {
            char **nameTbl = comp_name_table(compHdr);
            namePtr = nameTbl ? (uint8_t *)nameTbl[0] : (uint8_t *)_aCup_0;
        }
        *teamNamesPtr++ = (void *)namePtr;
        *leaguesPtrOut++ = compEntry;
        *leagueNumsOut++ = 0xFFFF;
        d1++;
        goto loc_E086;
    } else if (compType == 2) {
        /* Tournament — same as Cup */
        {
            char **nameTbl = comp_name_table(compHdr);
            namePtr = nameTbl ? (uint8_t *)nameTbl[0] : (uint8_t *)_aLeague_1;
        }
        *teamNamesPtr++ = (void *)namePtr;
        *leaguesPtrOut++ = compEntry;
        *leagueNumsOut++ = 0xFFFF;
        d1++;
        goto loc_E086;
    } else {
        _panicReasonCode = 0x2209;
        PanicHaltLoop();
    }
    }

loc_E2D0:
    _numLeaguesInCountry = d1;
    HLOG("enum done country=%u numLeagues=%u\n", (unsigned)(d7 & 0xFF), (unsigned)d1);

    /* Build country names list */
    countryNamesPtr = _countryNames;
    cnCount = 0;

    if (d7 == 254 || (d7 >= CN_EUROPE && d7 <= CN_OCEANIA)) {
        /* loc_13E2F4: enumerate countries from the QualifyingGroup's countries[]
         * list. After consolidation countries[] lives at a FIXED struct offset
         * (compCup[] is padded to max in the merged QualifyingGroup), so repoint
         * a5 to it. The compCup[] walk above left a5 just past the -1 terminator,
         * which now lands on compCup padding (reads as country 0 = ALBANIA). a5
         * stays pointed at countries[] so the `a5[index]` country lookups in the
         * selection handling below are correct too. See STRUCT_CONSOLIDATE.md §4d. */
        a5 = (void **)((QualifyingGroup *)compListAddr)->countries;
        cPtr = (uint8_t *)a5;
        for (;;) {
            cByte = *cPtr++;
            if (cByte == 255)
                break;
            cidx = (uint16_t)cByte;
            ctEntry = (uint8_t *)_countriesTable[cidx];
            *countryNamesPtr++ = (void *)(ctEntry + 1);
            cnCount++;
        }
    }

    _chooseTeamsNumTeams = cnCount;

    /* loc_13E318: prepare menu */
    _stayInSelectTeams = 0;
    _exitFlag = 0;

    if (_showCupsAndOther == 0 && _chooseTeamsNumTeams == 0 &&
        _numLeaguesInCountry == 1) {
        if (_exitSelectTeams != 0) {
            _exitFlag = 1;
        } else {
            _stayInSelectTeams = 1;
        }
    }

    /* loc_13E35E: show menu */
    GetSelectedTeamsStackTop();
    ShowMenu(_selectTeamsMenu);

    if (_chooseTeamsResult != 0)
        goto exit_return;
    if (_abortSelectTeams != 0)
        goto exit_return;

    d6 = d7;

    if (_viewSelectedTeams != 0)
        goto loc_E73A;

    /* Check selectedTeamIndex */
    d7 = (uint16_t)_selectedTeamIndex;
    if (_selectedTeamIndex >= 0)
        goto loc_E3DC;

    /* selectedTeamIndex < 0: check selectedLeagueIndex */
    d7 = (uint16_t)_selectedLeagueIndex;
    if (_selectedLeagueIndex < 0) {
        /* loc_13E3C4: go back */
        if (_teamDepthLevel == 0)
            goto exit_return;
        _isCountryNumber = 0;
        d7 = PopContinent();
        goto loc_E012;
    }

    PushLastSelectedTeam();

    if (_loadTeam != 0)
        goto loc_E6E4;
    if (_playSeason != 0)
        goto loc_E428;
    goto loc_E50A;

loc_E3DC:
    PushLastSelectedTeam();

    if (_showCupsAndOther != 0 || _playSeason != 0 || _teamsType != 0) {
        /* loc_13E3F8: push and navigate deeper */
        PushContinent(d6);
        d7 = ((uint8_t *)a5)[d7];
        SetSelectedTeamsStackTop();
        goto loc_E012;
    }

    /* loc_13E408: direct team load */
    countryByte = ((uint8_t *)a5)[d7];
    GetCountryName(countryByte & 0xFF);
    _leagueNumber = 0;
    LoadTeamFile((uint8_t)countryByte);
    goto loc_E6F6;

loc_E428:
    /* Play season: dispatch by competition type */
    contest = (uint8_t *)_leaguesPtrTable[d7];
    _selectedContest = (uintptr_t)contest;

    if (contest[1] == 0) {
        /* League: loc_13E476 */
        lnum = _leagueNumbersTable[d7];
        _leagueNumber = lnum;
        _contestLeagueNumber = lnum;
        InitCompetitionTeamSelectionFromTableEntry(lnum, contest);
    } else if (contest[1] == 2) {
        /* Tournament: loc_13E45A */
        _leagueNumber = 0;
        _contestLeagueNumber = 0;
        InitCompetitionTeamSelectionFromAltTableHeader(contest);
    } else {
        _panicReasonCode = 0x1789;
        PanicHaltLoop();
    }

    /* loc_13E49E: */
    _playWhatStr = (uintptr_t)_aPlaySeason;
    MarkSelectedTeamsStackLocation();
    _selectTeamsNoLimit = 0;
    _playSeason = 0;
    _loadTeam = 1;
    _chooseTeamsHeader1stPart = 0;
    _teamsType = 1;
    _gameTypeStr = _selectedTeamText;
    _viewSelectedTeams = 3;
    GetCountryName(d6);
    goto loc_E73A;

loc_E4EA:
    /* Country number navigation */
    d7 = (uint16_t)(uintptr_t)contest;
    PushLastSelectedTeam();
    PushContinent(d6);
    SetSelectedTeamsStackTop();
    _isCountryNumber = 1;
    _countryNumber = d6;
    goto loc_E012;

loc_E50A:
    /* Select league/competition: dispatch */
    contest = (uint8_t *)_leaguesPtrTable[d7];

    /* Career mode special checks (loc_13E50A) */
    if (_gameType == GAME_TYPE_CAREER) {
        if (contest == _europeanChampionships) {
            if (_someTeamFlags & (1 << 0))
                contest = (uint8_t *)0xFD;
        }
        if (contest == _worldCup) {
            if (_someTeamFlags & (1 << 6))
                contest = (uint8_t *)0xFC;
        }
    }

    /* loc_13E554: check if country entry (< 256) */
    if ((uintptr_t)contest < 256)
        goto loc_E4EA;

    _selectedContest = (uintptr_t)contest;

    /* The 36-team european presets (Champions/Europa/Conference League) play a
     * substituted TOURNAMENT record over the legacy euro-cup records, so the
     * type dispatch and the selection init must see the substitute (type 2).
     * _selectedContest keeps the ORIGINAL record: the continental-cup check,
     * the club/national split and the later PresetCompetitionMenu dispatch
     * (which re-substitutes from it) all key off the original. */
    playRec = contest;
    if (_choosingPreset != 0)
        playRec = Champions36_PresetRecord(contest);

    if (playRec[1] == 0)
        goto loc_E65A;
    else if (playRec[1] == 1)
        goto loc_E5EA;
    else
        goto loc_E57A;

loc_E57A:
    /* Tournament setup */
    if (_chooseCompetitionOnly != 0) {
        _chooseTeamsResult = 1;
        goto exit_return;
    }
    InitCompetitionTeamSelectionFromAltTableHeader(playRec);
    _playWhatStr = (uintptr_t)_aPlayTournament;
    SetTeamsTypeForSelectedContest();
    IsContestContinentalCup();
    MarkSelectedTeamsStackLocation();
    _selectTeamsNoLimit = 0;
    _showCupsAndOther = 0;
    _loadTeam = 1;
    _chooseTeamsHeader1stPart = 0;
    _gameTypeStr = _selectedTeamText;
    _viewSelectedTeams = 2;
    GetCountryName(d6);
    goto loc_E73A;

loc_E5EA:
    /* Cup setup */
    if (_chooseCompetitionOnly != 0) {
        _chooseTeamsResult = 1;
        goto exit_return;
    }
    InitCompetitionTeamSelectionFromTableHeader((uint8_t *)_selectedContest);
    _playWhatStr = (uintptr_t)_aPlayCup;
    SetTeamsTypeForSelectedContest();
    IsContestContinentalCup();
    MarkSelectedTeamsStackLocation();
    _selectTeamsNoLimit = 0;
    _showCupsAndOther = 0;
    _loadTeam = 1;
    _chooseTeamsHeader1stPart = 0;
    _gameTypeStr = _selectedTeamText;
    _viewSelectedTeams = 2;
    GetCountryName(d6);
    goto loc_E73A;

loc_E65A:
    /* League setup */
    lnum = _leagueNumbersTable[d7];
    _leagueNumber = lnum;
    _contestLeagueNumber = lnum;

    if (_chooseCompetitionOnly != 0) {
        _chooseTeamsResult = 1;
        goto exit_return;
    }
    InitCompetitionTeamSelectionFromTableEntry(lnum, (uint8_t *)_selectedContest);
    _playWhatStr = (uintptr_t)_aPlayLeague;
    SetTeamsTypeForSelectedContest();
    IsContestContinentalCup();
    MarkSelectedTeamsStackLocation();
    _selectTeamsNoLimit = 0;
    _showCupsAndOther = 0;
    _loadTeam = 1;
    _chooseTeamsHeader1stPart = 0;
    _gameTypeStr = _selectedTeamText;
    _viewSelectedTeams = 2;
    GetCountryName(d6);
    goto loc_E73A;

loc_E6E4:
    /* Load team file for selected league */
    _leagueNumber = d7;
    if (LoadTeamFile((uint8_t)d6) != 0)
        goto exit_clear;

loc_E6F6:
    /* Show friendly menu */
    _entryText2 = _selectedTeamText;
    ShowMenu(_chooseTeamsFriendlyMenu);

    if (_viewSelectedTeams != 0)
        goto loc_E73A;
    if (_abortSelectTeams != 0)
        goto exit_return;
    if (_chooseTeamsResult != 0)
        goto exit_return;

    /* loc_13E734: */
    d7 = d6;
    goto loc_E012;

loc_E73A:
    /* Show friendly menu 2 (with country name if needed) */
    if (_isCountryNumber != 0)
        GetCountryName(_countryNumber);
    _entryText2 = _selectedTeamText;
    ShowMenu(_chooseTeamsFriendlyMenu2);

    if (_abortSelectTeams != 0)
        goto exit_return;
    if (_chooseTeamsResult != 0)
        goto exit_return;

    if (_isCountryNumber != 0) {
        _isCountryNumber = 0;
        d7 = PopContinent();
        if (_isContestContinentalCup != 0)
            goto loc_E6F6;
        goto loc_E012;
    }

    /* loc_13E79A: */
    if (_isContestContinentalCup != 0)
        goto loc_E6F6;

    /* loc_13E734: */
    d7 = d6;
    goto loc_E012;

exit_clear:
    /* loc_13E7A6: */
    _chooseTeamsResult = 0;
exit_return:
    /* locret_13E7AC: */
    return;
}
