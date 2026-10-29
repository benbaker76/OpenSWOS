/* diy_import_dos.c -- load a .DIY written by the PC game or SWOS 2020.
 *
 * Both releases save a DIY competition the same way: everything from
 * competitionFileBuffer up to numSelectedTeams, then numSelectedTeams itself, then that
 * many 684-byte team records (SWOS2.s SaveDIYFile; swos.asm SaveDIYFile). The DOS region
 * is 5189 bytes against the Amiga's 5168 and its words are little-endian. The field order
 * is the Amiga's, with these differences (swos-port swos.asm, competitionFileBuffer..
 * g_numSelectedTeams, checked against the SWOS 2020 files):
 *
 *   - a DIYCompetition header is 0x5F bytes, not 0x60: its name is 35 bytes, so every
 *     header field after it sits one byte lower. The file holds two of them, the
 *     competition at 0 and (for a tournament) the current stage at 0x41F, so offsets are
 *     Amiga - 1 from 0x27 and Amiga - 2 inside the stage;
 *   - careerLoadedFlag is missing; the commentary/chairman option words are extra;
 *   - the tail has no version/magic words, and its random seeds are laid out differently.
 *
 * The file's region is the LEGACY competitionFileBuffer layout this port untangled (see
 * DIYTournamentState / CompetitionSlotBuffer in game_types.h), so each part goes to the
 * named field that replaced it:
 *
 *   tournament (type 2)  0x000 header, 0x059 teamIndexOrder (overlaying the header tail),
 *                        0x099 teamIds, 0x119 groupMap, 0x159 slots[6],
 *                        0x41F the current stage, a DIYCompetitionEx (below)
 *   league/cup (0/1)     0x000 a DIYCompetitionEx (below)
 *   DIYCompetitionEx     0x000 header, 0x05F body to bracketScratch's end, 0x2B3 the league
 *                        standings, which in the legacy layout overlaid cupTeamIds[51]..
 *
 * Team selections in the standings and the cup bracket were byte offsets from the teams
 * array; here they are the team's index (diy_team_to_identity).
 *
 * Words are copied as bytes, so this is only right on a little-endian host, where a DOS
 * word in memory is already the host-native field and a {file, ordinal} team id is the
 * same byte pair. A big-endian host (the Amiga) refuses the file, as it did before. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>

#include "diy_import_dos.h"
#include "game_types.h"
#include "save_format_json.h"
#include "swos_io.h"

extern CompetitionSlotBuffer _primaryCompetitionBuf;
extern TeamFileEx  _selectedTeams[100];
extern uint16_t    _numSelectedTeams;
extern uint16_t    _careerFileBuffer;
extern uint16_t    _gameLength, _autoReplays, _menuMusic, _autoSaveHighlights;
extern uint16_t    _allPlayerTeamsEqual, _pitchType;
extern uint16_t    _saveFileVersion;
extern uint32_t    _saveFileAmigaType;
extern TeamTactics _teamTacticsUSERA, _teamTacticsUSERB, _teamTacticsUSERC;
extern TeamTactics _teamTacticsUSERD, _teamTacticsUSERE, _teamTacticsUSERF;

enum {
    kTeamRecord      = 684,
    kRegion          = 5189,          /* competitionFileBuffer .. g_numSelectedTeams */
    kNumTeamsOffset  = 0x31,          /* DIYCompetition.numTeams (Amiga 0x32) */
    kHeader          = 0x5F,          /* DIYCompetition (Amiga 0x60) */
    kNameLen         = 35,
    kNamePtr         = 0x27,
    kHeaderTail      = 0x2B,          /* compSlot .. playEachTeam */
    kExStandings     = 0x2B3,         /* legacy standings overlay (Amiga 0x2B4) */
    kExLen           = 0x733,         /* the stage image (Amiga 0x734) */
    kTournOrder      = 0x59,
    kTournTeamIds    = 0x99,
    kTournGroupMap   = 0x119,
    kTournSlots      = 0x159,
    kTournStage      = 0x41F,
    kTactics         = 0xB5E,         /* USER_A..USER_F, 370 bytes each */
    kOptions         = 0x140A,        /* gameLength .. pitchType */
    kMaxTeams        = 64,
    kAmigaMaxTeams   = 64,            /* a DIY's own limit (teams.c); 48 proved on the Amiga */
};

extern uint16_t _gameType;

/* The header fields after namePtr are one contiguous run on every target. */
typedef char _diy_dos_header_tail_check[
    (offsetof(DIYCompetition, playEachTeam) + 2 - offsetof(DIYCompetition, compSlot)
        == kHeader - kHeaderTail) ? 1 : -1];
/* The DIYCompetitionEx body, minSubstitutes up to the relocated standings: the legacy
 * 0x60..0x5C8 (in which the standings overlay sat). */
typedef char _diy_dos_body_check[
    (offsetof(DIYCompetitionEx, leagueStandings) - offsetof(DIYCompetitionEx, minSubstitutes)
        == 0x5C8 - 0x60) ? 1 : -1];
typedef char _diy_dos_standings_check[
    (sizeof(((DIYCompetitionEx *)0)->leagueStandings) == kExLen - kExStandings) ? 1 : -1];

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int32_t DiyImportDos_IsDosFile(const uint8_t *data, uint32_t size)
{
    uint16_t type, teams;

    if (size < kRegion + 2 || data[0] == '{')
        return 0;
    type  = le16(data);
    teams = le16(data + kRegion);
    return type <= 2 && teams >= 1 && teams <= kMaxTeams &&
           le16(data + kNumTeamsOffset) == teams &&
           size == (uint32_t)kRegion + 2 + (uint32_t)teams * kTeamRecord;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static void header_from_dos(DIYCompetition *h, const uint8_t *src)
{
    memcpy(h, src, offsetof(DIYCompetition, competitionName));
    memcpy(h->competitionName, src + 4, kNameLen);
    h->competitionName[kNameLen] = 0;
    h->namePtr = (uint8_t *)h->competitionName;
    memcpy(&h->compSlot, src + kHeaderTail, kHeader - kHeaderTail);
}

/* A selection that was a byte offset from the teams array becomes the team's index. */
static int32_t team_identity(int32_t offset, int32_t teams)
{
    if (offset >= 0 && offset % kTeamRecord == 0 && offset / kTeamRecord < teams)
        return offset / kTeamRecord;
    return offset;
}

static void ex_from_dos(DIYCompetitionEx *ex, const uint8_t *src, int32_t league,
                        int32_t teams)
{
    header_from_dos(&ex->base, src);
    memcpy(&ex->minSubstitutes, src + kHeader,
           offsetof(DIYCompetitionEx, leagueStandings) - offsetof(DIYCompetitionEx, minSubstitutes));
    if (league) {
        memcpy(ex->leagueStandings, src + kExStandings, sizeof ex->leagueStandings);
        for (int32_t i = 0; i < 64; i++)
            ex->leagueStandings[i].teamSelOffset =
                team_identity(ex->leagueStandings[i].teamSelOffset, teams);
    } else {
        for (int32_t i = 0; i < 64; i++)
            ex->bracketScratch.teamOffsetTable[i] =
                team_identity(ex->bracketScratch.teamOffsetTable[i], teams);
    }
}

int16_t DiyImportDos_Load(uint8_t *filename)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    int16_t r;

    if (swos_io_load_alloc(filename, &data, &size) != 0)
        return 1;
    r = DiyImportDos_LoadData((const char *)filename, data, size, 0);
    swos_io_free(data);
    return r;
}

int16_t DiyImportDos_LoadData(const char *label, const uint8_t *data, uint32_t size,
                              int32_t keepSettings)
{
    TeamTactics *const tactics[6] = {
        &_teamTacticsUSERA, &_teamTacticsUSERB, &_teamTacticsUSERC,
        &_teamTacticsUSERD, &_teamTacticsUSERE, &_teamTacticsUSERF,
    };
    uint16_t *const options[6] = {
        &_gameLength, &_autoReplays, &_menuMusic, &_autoSaveHighlights,
        &_allPlayerTeamsEqual, &_pitchType,
    };
    uint16_t type, teams;

    if (!DiyImportDos_IsDosFile(data, size))
        return 1;
    type  = le16(data);
    teams = le16(data + kRegion);

    /* What a JSON .DIY load zeroes (the comp-tagged fields), then the file over it. */
    memset(&_primaryCompetitionBuf, 0, sizeof _primaryCompetitionBuf);
    _careerFileBuffer = 0;
    _saveFileVersion = 0;
    _saveFileAmigaType = 0;

    if (type == 2) {
        DIYTournamentState *t = &_primaryCompetitionBuf.tourn;
        DIYCompetitionEx *stage = (DIYCompetitionEx *)_primaryCompetitionBuf.stageSnapshot;
        int16_t active;

        header_from_dos(&t->header, data);
        t->leagueWarningsEnabled = le16(data + kHeader);
        memcpy(t->teamIndexOrder, data + kTournOrder, sizeof t->teamIndexOrder);
        memcpy(t->teamIds, data + kTournTeamIds, sizeof t->teamIds);
        memcpy(t->groupMap, data + kTournGroupMap, sizeof t->groupMap);
        memcpy(t->slots, data + kTournSlots, sizeof t->slots);
        active = t->header.roundComplete;   /* the stage in progress */
        ex_from_dos(stage, data + kTournStage,
                    active < 0 || active >= 6 || t->slots[active].isLeagueFormat != 0, teams);
    } else {
        ex_from_dos((DIYCompetitionEx *)&_primaryCompetitionBuf, data, type == 0, teams);
    }

    for (int32_t i = 0; i < 6 && !keepSettings; i++) {
        memcpy(tactics[i], data + kTactics + i * sizeof(TeamTactics), sizeof(TeamTactics));
        *options[i] = le16(data + kOptions + i * 2);
    }

    memset(_selectedTeams, 0, sizeof _selectedTeams);
    memcpy(_selectedTeams, data + kRegion + 2, (size_t)teams * kTeamRecord);
    _numSelectedTeams = teams;

    fprintf(stderr, "[diy-import] %s: a PC/SWOS 2020 %s, \"%s\", %u teams\n", label,
            type == 0 ? "league" : type == 1 ? "cup" : "tournament",
            (const char *)_primaryCompetitionBuf.tourn.header.competitionName,
            (unsigned)teams);
    return 0;
}

#else

int16_t DiyImportDos_Load(uint8_t *filename)
{
    return DiyImportDos_LoadData((const char *)filename, NULL, 0, 0);
}

int16_t DiyImportDos_LoadData(const char *label, const uint8_t *data, uint32_t size,
                              int32_t keepSettings)
{
    (void)data; (void)size; (void)keepSettings;
    fprintf(stderr, "[diy-import] %s: a PC/SWOS 2020 .DIY is not loadable on this target\n", label);
    return 1;
}

#endif

/* ---- --convert-diy: a PC/SWOS 2020 .DIY as this game's JSON .DIY ---- */

static char s_convertIn[1024], s_convertOut[1024];

static int32_t absolute(const char *path, char *out, size_t cap)
{
    char cwd[1024];

    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':'))
        return snprintf(out, cap, "%s", path) < (int)cap ? 0 : 1;
    if (!getcwd(cwd, sizeof cwd))
        return 1;
    return snprintf(out, cap, "%s/%s", cwd, path) < (int)cap ? 0 : 1;
}

int32_t DiyConvert_Request(const char *arg)
{
    const char *comma = strchr(arg, ',');
    char in[1024];

    if (!comma || comma == arg || !comma[1] || (size_t)(comma - arg) >= sizeof in)
        return 1;
    memcpy(in, arg, (size_t)(comma - arg));
    in[comma - arg] = 0;
    return absolute(in, s_convertIn, sizeof s_convertIn) ||
           absolute(comma + 1, s_convertOut, sizeof s_convertOut);
}

int32_t DiyConvert_Armed(void) { return s_convertIn[0] != 0; }

/* Exits: 0 converted, 1 failed, 2 refused (more teams than a DIY competition holds). */
void DiyConvert_Run(void)
{
    static uint8_t scratch[] = "_CONVERT.DIY";     /* in the save folder, then moved */
    char written[1280];
    uint8_t *data;
    long n;
    FILE *f = fopen(s_convertIn, "rb");

    if (!f) {
        fprintf(stderr, "[diy-convert] cannot read %s\n", s_convertIn);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    if (!data || n <= 0 || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "[diy-convert] cannot read %s\n", s_convertIn);
        exit(1);
    }
    fclose(f);

    if (!DiyImportDos_IsDosFile(data, (uint32_t)n)) {
        fprintf(stderr, "[diy-convert] %s is not a PC/SWOS 2020 .DIY\n", s_convertIn);
        exit(1);
    }
    if (le16(data + kRegion) > kAmigaMaxTeams) {
        fprintf(stderr, "[diy-convert] %s: %u teams, more than a DIY holds (%d): not converted\n",
                s_convertIn, (unsigned)le16(data + kRegion), (int)kAmigaMaxTeams);
        exit(2);
    }
    _gameType = 1;          /* a DIY competition, as LoadDIYFile sets it */
    if (DiyImportDos_LoadData(s_convertIn, data, (uint32_t)n, 0) != 0) {
        fprintf(stderr, "[diy-convert] %s: not converted\n", s_convertIn);
        exit(1);
    }
    /* A classic competition starts with every team COMPUTER, as CLASSICS starts it
     * (classic_comps.c): SWOS 2020's cups make them all COACH. */
    for (int32_t i = 0; i < _numSelectedTeams; i++)
        _selectedTeams[i].team.teamControls = 1;   /* TEAM_COMPUTER */
    if (SaveJson_WriteDIY(scratch) != 0) {
        fprintf(stderr, "[diy-convert] %s: not converted\n", s_convertIn);
        exit(1);
    }
    free(data);

    snprintf(written, sizeof written, "%s/%s", swos_save_dir(), (const char *)scratch);
    remove(s_convertOut);
    if (rename(written, s_convertOut) != 0) {
        fprintf(stderr, "[diy-convert] wrote %s but cannot move it to %s\n", written, s_convertOut);
        exit(1);
    }
    fprintf(stderr, "[diy-convert] %s -> %s\n", s_convertIn, s_convertOut);
    exit(0);
}
