/* National player search: worldwide, current career rosters.
 * Separate packed SWOS menu so it works with mouse and keyboard.
 * No persistent cache: indexes are rebuilt each time the search opens.
 */
#include "menu_data.h"
#include "menus.h"
#include "teams.h"
#include "career_teams.h"
#include "endian_helpers.h"
#include "swos_io.h"
#include "swos_identity_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

/* A fresh, flushed diagnostic log is created every time Search opens.
 * Relative path is the process working directory (normally bin/). */
static FILE *s_log;
static void search_log(const char *fmt, ...) {
    if (!s_log) return;
    time_t now = time(NULL);
    struct tm *stamp = localtime(&now);
    if (stamp) fprintf(s_log, "%02d:%02d:%02d ", stamp->tm_hour, stamp->tm_min, stamp->tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(s_log, fmt, args);
    va_end(args);
    fputc('\n', s_log);
    fflush(s_log); /* important: preserve last checkpoint if game freezes */
}
static void search_log_close(void) {
    if (s_log) { search_log("SEARCH CLOSED"); fclose(s_log); s_log=NULL; }
}
#define SEARCH_CAP 65536
#define SEARCH_ROWS 10

typedef struct SearchPlayer {
    uint16_t team;
    uint8_t fileIndex;
    uint8_t position;
    uint16_t valueK;
    uint8_t age;
    char name[24];
    char club[25];
} SearchPlayer;

static SearchPlayer s_players[SEARCH_CAP];
static int s_count, s_filtered[SEARCH_CAP], s_visible;
static int s_filter, s_page, s_action, s_selected, s_min, s_max;

extern uint8_t _nationalTeamNationality;
extern uint8_t _teamFileBuffer[];
extern uint16_t _teamFileTeamCount;
extern uint16_t _teamsLoaded;
extern uint16_t _skipLeagueNumbers;
extern uint8_t _lastLoadedExtNumber;
extern uint16_t _avgTeamValueBases[];
extern int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);
extern int16_t LoadTeamFile(uint16_t extNumber);
extern TeamFileEx *GetTeamPointer(uint16_t teamId);
extern uint16_t GetDisplayedPlayerValue(uint8_t *ptr);
extern void SetExitMenuFlag(void);
extern void Career_AddInternationalPlayerCommon(uint8_t *selectedTeam, uint8_t *title);
extern uint8_t _aAddInternation_0[];
extern void ShowMenu(uint8_t *menu);
extern void ShowPlayerOfferMenuForPlayer(uint16_t teamNumber, uint16_t playerOrdinal);
extern uint16_t _transferCompleted;
extern void SWOS_IdentityRegisterClub(unsigned club,unsigned file,unsigned record);
extern uint16_t _numForeignMarketPlayers2;
extern MarketPlayer _careerForeignMarketPlayers[];

/* Use the same live-cache precedence as GetTeamPointer(). The direct TEAM
 * reader below remains necessary for speed and for inactive clubs, but it
 * must not show an obsolete roster when the game is using a cached copy. */
extern uint16_t _currentTeamNumber, _selTeamNumber;
extern uint8_t _currentTeam[];
extern TeamFileEx _careerTeam;
extern uint16_t _numSelectedTeams, _careerFileBuffer;
extern TeamFileEx _selectedTeams[];
extern uint8_t _tmdFileBuffer[];
extern uint16_t _careerAutoAdvanceInProgress;
static const TeamFileEx *world_live_team(uint16_t id)
{
    if (id == _currentTeamNumber) {
        const TeamFileEx *t = (const TeamFileEx *)_currentTeam;
        if (PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal) == id)
            return t;
    }
    if (id == _selTeamNumber ||
        (_careerAutoAdvanceInProgress &&
         PACK_BE_U16(_careerTeam.team.countryNumber,
                     _careerTeam.team.teamOrdinal) == id)) {
        if (PACK_BE_U16(_careerTeam.team.countryNumber,
                        _careerTeam.team.teamOrdinal) == id)
            return &_careerTeam;
    }
    unsigned n = _numSelectedTeams;
    if (n > 100u) n = 100u;
    for (unsigned i = 0; i < n; ++i) {
        const TeamFileEx *t = &_selectedTeams[i];
        if (PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal) == id)
            return t;
    }
    n = _careerFileBuffer;
    if (n > 128u) n = 128u;
    const TeamFileEx *tmd = (const TeamFileEx *)_tmdFileBuffer;
    for (unsigned i = 0; i < n; ++i)
        if (PACK_BE_U16(tmd[i].team.countryNumber,
                        tmd[i].team.teamOrdinal) == id)
            return &tmd[i];
    return NULL;
}

/* Fast index: team ID + physical player slot -> newest career replacement.
 * Avoid GetTeamPointer(), which reloads the country file and loops over
 * the entire replacement pool for every individual club. */
#define REPLACEMENT_HASH_SIZE 131072u
static uint32_t s_replacementKeys[REPLACEMENT_HASH_SIZE];
static uint16_t s_replacementRefs[REPLACEMENT_HASH_SIZE];
static uint8_t s_replacementUsed[REPLACEMENT_HASH_SIZE];

static uint32_t replacement_key(uint16_t club, unsigned slot) {
    return ((uint32_t)club << 4) | (slot & 15u);
}
static unsigned replacement_bucket(uint32_t key) {
    return (unsigned)((key * 2654435761u) & (REPLACEMENT_HASH_SIZE - 1u));
}
static void index_replacements(void) {
    search_log("REPLACEMENTS BEGIN count=%u", (unsigned)_numForeignMarketPlayers2);
    memset(s_replacementUsed, 0, sizeof(s_replacementUsed));
    for (uint32_t i = 0; i < _numForeignMarketPlayers2; ++i) {
        if ((i % 1024u) == 0u) search_log("REPLACEMENTS progress %u/%u", (unsigned)i, (unsigned)_numForeignMarketPlayers2);
        MarketPlayer *entry = &_careerForeignMarketPlayers[i];
        if (entry->playerOrdinal < 0 || entry->playerOrdinal >= 16) continue;
        const uint8_t *bytes = (const uint8_t *)&entry->teamId;
        uint16_t club = PACK_BE_U16(bytes[0], bytes[1]);
        uint32_t key = replacement_key(club, (unsigned)entry->playerOrdinal);
        unsigned h = replacement_bucket(key);
        while (s_replacementUsed[h] && s_replacementKeys[h] != key)
            h = (h + 1u) & (REPLACEMENT_HASH_SIZE - 1u);
        s_replacementUsed[h] = 1;
        s_replacementKeys[h] = key;
        s_replacementRefs[h] = (uint16_t)i;
    }
    search_log("REPLACEMENTS DONE");
}
static const PlayerFile *replacement_for(uint16_t club, unsigned slot) {
    uint32_t key = replacement_key(club, slot);
    unsigned h = replacement_bucket(key);
    while (s_replacementUsed[h]) {
        if (s_replacementKeys[h] == key)
            return &_careerForeignMarketPlayers[s_replacementRefs[h]].player;
        h = (h + 1u) & (REPLACEMENT_HASH_SIZE - 1u);
    }
    return NULL;
}

static char s_title[]="WORLD PLAYER SEARCH";
static char s_rows[SEARCH_ROWS][75];
static char s_filterText[35], s_minText[35], s_maxText[35], s_pageText[40];
static const unsigned s_priceBands[] = {0,50,100,250,500,1000,2000,5000,10000,30000,65535};
/* SWOS positionAndFace bits 5..7: GK RB LB D RW LW M ATT.
 * Search filter 0 means ALL; encoded position + 1 is the filter ID. */
static const char *s_posLabel[] = {"ALL", "GK", "RB", "LB", "D", "RW", "LW", "M", "ATT"};
#define SEARCH_POSITION_FILTERS (sizeof(s_posLabel) / sizeof(s_posLabel[0]))

static void search_row0(void),search_row1(void),search_row2(void),search_row3(void),search_row4(void);
static void search_row5(void),search_row6(void),search_row7(void),search_row8(void),search_row9(void);
static void search_filter(void),search_min(void),search_max(void),search_prev(void),search_next(void),search_exit(void);

/* SWOS menu navigation uses absolute menu entry ordinals, including
 * non-selectable text entries. The title is entry 0, column heading 1,
 * filter 2, sort 3, player rows 4..13, pager 14, PREV 15, NEXT 16,
 * and EXIT 17. The earlier P12 links assumed rows 1..10, making arrow
 * navigation point into non-selectable headings and unrelated controls. */
#pragma pack(push,1)
typedef struct SearchMenu {
    MenuHeader hdr;
    MenuMark mark;
    MenuEntry_Byte_Colors_TextPtr title;
    MenuEntry_Byte_Colors_TextPtr columnHead;
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect filter, minPrice, maxPrice;
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect rows[SEARCH_ROWS];
    MenuEntry_Byte_Colors_TextPtr pager;
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect prev, next, exit;
    MenuMark end;
} SearchMenu;
#pragma pack(pop)

enum {
    SEARCH_FILTER_ENTRY = 2,
    SEARCH_MIN_ENTRY = 3,
    SEARCH_MAX_ENTRY = 4,
    SEARCH_FIRST_ROW_ENTRY = 5,
    SEARCH_LAST_ROW_ENTRY = SEARCH_FIRST_ROW_ENTRY + SEARCH_ROWS - 1,
    SEARCH_PREV_ENTRY = 16,
    SEARCH_NEXT_ENTRY = 17,
    SEARCH_EXIT_ENTRY = 18,
};
static char s_colHead[]="PLAYER (AGE)         CLUB       POS   VALUE";
#define ROW(y,i,fn) { { 8, (y), -16, 9 }, \
    { MENU_CMD_NAV, SEARCH_FILTER_ENTRY, SEARCH_MIN_ENTRY, \
      ((i)==0 ? SEARCH_MAX_ENTRY : (SEARCH_FIRST_ROW_ENTRY+(i)-1)), \
      ((i)==SEARCH_ROWS-1 ? SEARCH_PREV_ENTRY : (SEARCH_FIRST_ROW_ENTRY+(i)+1)) }, \
    {MENU_CMD_COLORS,0xE}, {MENU_CMD_TEXT_PTR,STR_ALIGN_LEFT,s_rows[i]}, \
    {MENU_CMD_ONSELECT_FIRE,fn}, MENU_CMD_END }
#define BTN(x,y,w,left,right,up,down,str,fn) { {x,y,w,13}, \
    {MENU_CMD_NAV,left,right,up,down}, {MENU_CMD_COLORS,0xE}, \
    {MENU_CMD_TEXT_PTR,0x0,str}, {MENU_CMD_ONSELECT_FIRE,fn}, MENU_CMD_END }
static SearchMenu s_menu={
    {0,0,0,SEARCH_FIRST_ROW_ENTRY}, MENU_MARK_BYTE_COORDS,
    {{48,4,-96,14},{MENU_CMD_COLORS,0x17},{MENU_CMD_TEXT_PTR,0,s_title},MENU_CMD_END},
    {{15,63,-28,9},{MENU_CMD_COLORS,0x0},{MENU_CMD_TEXT_PTR,STR_ALIGN_LEFT,s_colHead},MENU_CMD_END},
    BTN(8,23,145, SEARCH_MAX_ENTRY,SEARCH_MIN_ENTRY,SEARCH_EXIT_ENTRY,SEARCH_MAX_ENTRY,s_filterText,search_filter),
    BTN(163,23,145,SEARCH_FILTER_ENTRY,SEARCH_MAX_ENTRY,SEARCH_EXIT_ENTRY,SEARCH_MAX_ENTRY,s_minText,search_min),
    BTN(80,42,160,SEARCH_MIN_ENTRY,SEARCH_FILTER_ENTRY,SEARCH_MIN_ENTRY,SEARCH_FIRST_ROW_ENTRY,s_maxText,search_max),
    { ROW(74,0,search_row0), ROW(84,1,search_row1), ROW(94,2,search_row2),
      ROW(104,3,search_row3), ROW(114,4,search_row4), ROW(124,5,search_row5),
      ROW(134,6,search_row6), ROW(144,7,search_row7), ROW(154,8,search_row8),
      ROW(164,9,search_row9) },
    {{164,178,143,10},{MENU_CMD_COLORS,0x0},{MENU_CMD_TEXT_PTR,0,s_pageText},MENU_CMD_END},
    BTN(14,187,67, SEARCH_EXIT_ENTRY,SEARCH_NEXT_ENTRY,SEARCH_LAST_ROW_ENTRY,SEARCH_FILTER_ENTRY,"PREV",search_prev),
    BTN(90,187,67, SEARCH_PREV_ENTRY,SEARCH_EXIT_ENTRY,SEARCH_LAST_ROW_ENTRY,SEARCH_MIN_ENTRY,"NEXT",search_next),
    BTN(236,187,71,SEARCH_NEXT_ENTRY,SEARCH_PREV_ENTRY,SEARCH_LAST_ROW_ENTRY,SEARCH_MAX_ENTRY,"EXIT",search_exit),
    MENU_MARK_END_MENU
};

static void search_choose(int n) { s_action=1; s_selected=n; SetExitMenuFlag(); }
static void search_row0(void){search_choose(0);} static void search_row1(void){search_choose(1);}
static void search_row2(void){search_choose(2);} static void search_row3(void){search_choose(3);}
static void search_row4(void){search_choose(4);} static void search_row5(void){search_choose(5);}
static void search_row6(void){search_choose(6);} static void search_row7(void){search_choose(7);}
static void search_row8(void){search_choose(8);} static void search_row9(void){search_choose(9);}
static void search_filter(void){s_action=2;SetExitMenuFlag();}
static void search_min(void){s_action=3;SetExitMenuFlag();}
static void search_max(void){s_action=7;SetExitMenuFlag();}
static void search_prev(void){s_action=4;SetExitMenuFlag();}
static void search_next(void){s_action=5;SetExitMenuFlag();}
static void search_exit(void){s_action=6;SetExitMenuFlag();}

static int valid_country(int n){
    /* Club files included in the SWOS 96/97 DATA distribution.  The skipped
       numbers are reserved / have no TEAM.NNN file, and LoadTeamFile panics. */
    /* Keep the searches consistent with the AI transfer collector:
     * TEAM.072 contains special/custom teams (including SENSIBLE FC),
     * not normal career clubs. Other excluded slots are likewise not
     * ordinary career-club data. Do not exclude TEAM.000 (Albania). */
    return n>=0 && n<80 && n!=9 && n!=52 && n!=53 && n!=54 &&
           n!=56 && n!=57 && n!=58 && n!=59 && n!=61 && n!=63 &&
           n!=70 && n!=72 && n!=74;
}
static void safe_copy(char *dst,size_t size,const char *src,size_t max){
    size_t i=0;
    while (i+1<size && i<max && src[i] && (unsigned char)src[i]!=0xFF){dst[i]=src[i];i++;}
    dst[i]=0;
}
/* Read the raw TEAM.NNN files ourselves. LoadTeamFile() is an interactive
 * engine asset loader: on load errors it may retry forever / enter a panic
 * loop. A worldwide read-only query must never invoke that code path.
 * The stock TEAM files have a BE two-byte count followed by 684-byte teams.
 * All player fields and the two team-ID bytes are byte-sized. */
#define RAW_TEAM_LIMIT 93
static uint8_t s_rawCountry[2 + RAW_TEAM_LIMIT * sizeof(TeamFileEx)];
/* Keep compressed bytes separate: decompression cannot be done in-place. */
static uint8_t s_compressedCountry[sizeof(s_rawCountry)];
extern void RncDecompressV1(uint8_t *src, uint8_t *dst);
extern void RncDecompressV2(uint8_t *src, uint8_t *dst);

/* The game data is normally installed under the user's SWOS assets directory,
 * not necessarily beside bin/SWOS.exe. Resolve it the same way as LoadFileEx.
 * swos_data_path() also provides case-insensitive lookup on desktop builds. */
static FILE *open_country_file(int country, char *chosen, size_t cap) {
    char rel[32];
    char resolved[1024];
    snprintf(rel, sizeof rel, "data/team.%03d", country);
    const char *path=swos_data_path(rel, resolved, (uint32_t)sizeof resolved);
    FILE *f=fopen(path,"rb");
    snprintf(chosen,cap,"%s",path);
    return f;
}

/* Use the career's permanent identities, including post-retirement players. */
static unsigned world_player_age(uint16_t club, unsigned slot) {
    unsigned age=0, id=0;
    for (unsigned i=0, count=SWOS_GeneratedIdentityCount(); i<count; ++i) {
        unsigned g[4];
        if (SWOS_GeneratedIdentityGet(i,g) && g[0]==club && g[1]==slot &&
            g[2] && g[3]>=15 && g[3]<=60)
            return g[3];
    }
    if (SWOS_ManualIdentityIsTouched(club,slot)) {
        if (SWOS_ManualIdentityAge(club,slot,&age,&id) && id && age>=15 && age<=60)
            return age;
        return 25;
    }
    if (SWOS_IdentityAgeForClub(club,slot,&age,&id) && id && age>=15 && age<=60)
        return age;
    return 25;
}

static void scan_world(void){
    s_count=0;
    search_log("SCAN BEGIN worldwide");
    index_replacements();
    int loaded=0,missing=0,invalid=0;
    /* Countries 80..85 contain national-team squads, not clubs.
     * Those records must never be offered as call-up sources. */
    search_log("CLUB-ONLY SCAN: excluding national team databases 80..85");
    for (int c=0;c<CN_EUROPE;c++) {
        if (!valid_country(c)) continue;
        char filename[1024];
        unsigned count=0;
        int jsonLoaded=0;
        /* Normally OpenSWOS uses data_json/ when available. This reader uses
         * the same parser but never calls LoadTeamFile(), avoiding its fatal
         * error/retry path and its global team-buffer side effects. */
        const char *dj=getenv("SWOS_DATA_JSON");
        if (!(dj && dj[0]=='0' && dj[1]=='\0')) {
            int n=DataJson_LoadTeamFileForCountry(c,s_rawCountry,RAW_TEAM_LIMIT);
            if (n>0 && n<=RAW_TEAM_LIMIT) {
                jsonLoaded=1;
                count=(unsigned)n;
                search_log("COUNTRY %d JSON OK count=%u",c,count);
            }
        }
        if (!jsonLoaded) {
            FILE *f=open_country_file(c,filename,sizeof(filename));
            if (!f) {
                search_log("COUNTRY %d SKIP no JSON or binary (resolved %s)",c,filename);
                missing++;
                continue;
            }
            size_t bytes=fread(s_compressedCountry,1,sizeof(s_compressedCountry),f);
            int excess=(fgetc(f)!=EOF);
            int readError=ferror(f);
            fclose(f);
            if (readError || bytes<2 || excess) {
                search_log("COUNTRY %d INVALID read bytes=%u overflow=%d ioerror=%d",
                           c,(unsigned)bytes,excess,readError);
                invalid++;
                continue;
            }
            /* TEAM files in the OpenSWOS assets tree are RNC-compressed.
             * The two leading 'RN' bytes (0x524E=21070) are NOT a team
             * count. Decode into a separate, sufficiently large buffer. */
            if (bytes >= 18 && s_compressedCountry[0]=='R' &&
                s_compressedCountry[1]=='N' && s_compressedCountry[2]=='C' &&
                (s_compressedCountry[3]==1 || s_compressedCountry[3]==2)) {
                uint32_t unpacked=BE_READ_U32(s_compressedCountry+4);
                uint32_t packed=BE_READ_U32(s_compressedCountry+8);
                if (unpacked<2 || unpacked>sizeof(s_rawCountry) ||
                    packed>bytes-18 || unpacked>2+RAW_TEAM_LIMIT*sizeof(TeamFileEx)) {
                    search_log("COUNTRY %d INVALID RNC unpacked=%u packed=%u bytes=%u",
                               c,(unsigned)unpacked,(unsigned)packed,(unsigned)bytes);
                    invalid++;
                    continue;
                }
                search_log("COUNTRY %d RNC DECODE BEGIN method=%u unpacked=%u",
                           c,(unsigned)s_compressedCountry[3],(unsigned)unpacked);
                if (s_compressedCountry[3]==1)
                    RncDecompressV1(s_compressedCountry,s_rawCountry);
                else
                    RncDecompressV2(s_compressedCountry,s_rawCountry);
                bytes=unpacked;
                search_log("COUNTRY %d RNC DECODE DONE",c);
            } else {
                memcpy(s_rawCountry,s_compressedCountry,bytes);
            }
            count=((unsigned)s_rawCountry[0]<<8)|s_rawCountry[1];
            if (count>RAW_TEAM_LIMIT || bytes!=2+count*sizeof(TeamFileEx)) {
                search_log("COUNTRY %d INVALID count=%u bytes=%u",c,count,(unsigned)bytes);
                invalid++;
                continue;
            }
            search_log("COUNTRY %d RAW OK count=%u file=%s",c,count,filename);
        }
        loaded++;
        for (unsigned t=0;t<count;t++) {
            TeamFileEx *team=(TeamFileEx*)(s_rawCountry+2+t*sizeof(TeamFileEx));
            /* A defensive check against national teams appearing within
             * an unexpected club-country file. */
            if (team->team.countryNumber >= CN_EUROPE &&
                team->team.countryNumber <= CN_OCEANIA) continue;
            uint16_t id=PACK_BE_U16(team->team.countryNumber,team->team.teamOrdinal);
            /* Never offer a manager the players already at their own club.
             * Use the selected career team ID, not a name or roster pointer:
             * both can differ from the original TEAM data after transfers. */
            if (id == _selTeamNumber) {
                search_log("SKIP MANAGED CLUB team=%04X", (unsigned)id);
                continue;
            }
            /* Direct TEAM-file reads bypass LoadTeamFile and its identity mapping.
             * Register the actual record index, not the team ordinal: they
             * need not be identical. This makes original player ages available. */
            SWOS_IdentityRegisterClub(id,(unsigned)c,t);
            /* Match GetTeamPointer: current/managed/selected/TMD cache first,
             * otherwise the newest foreign-market patches over TEAM.NNN. */
            const TeamFileEx *live = world_live_team(id);
            if (live) {
                memcpy(team->players, live->players, sizeof(team->players));
                search_log("LIVE_ROSTER club=%04X source=career_cache", (unsigned)id);
            } else {
                for (unsigned slot=0;slot<16;slot++) {
                    const PlayerFile *patch=replacement_for(id,slot);
                    if (patch) memcpy(&team->players[slot],patch,sizeof(PlayerFile));
                }
            }
            for (unsigned i=0;i<16;i++) {
                PlayerFile *p=&team->players[i];
                if ((uint8_t)p->playerName[0]==0xFF || p->playerName[0]==0) continue;
                if (s_count>=SEARCH_CAP) break;
                SearchPlayer *dst=&s_players[s_count++];
                dst->team=id;
                dst->fileIndex=(uint8_t)i;
                unsigned pos=((unsigned)p->positionAndFace & PF_POSITION_MASK)>>5;
                dst->position=(uint8_t)(pos + 1);
                uint16_t val=GetDisplayedPlayerValue((uint8_t*)team+i*sizeof(PlayerFile));
                if (val>80)val=80;
                dst->valueK=_avgTeamValueBases[val];
                dst->age=(uint8_t)world_player_age(id,i);
                safe_copy(dst->name,sizeof(dst->name),(const char*)p->playerName,sizeof(p->playerName));
                safe_copy(dst->club,sizeof(dst->club),(const char*)team->team.teamName,sizeof(team->team.teamName));
            }
        }
    }
    search_log("SCAN DONE countries=%d missing=%d invalid=%d matches=%d",
               loaded,missing,invalid,s_count);
}

static int compar_player(const void*a,const void*b){
    const SearchPlayer *x=&s_players[*(const int*)a],*y=&s_players[*(const int*)b];
    int d=(int)y->valueK-(int)x->valueK;
    return d?d:strcmp(x->name,y->name);
}
static void build_list(void){
    s_visible=0;
    for(int i=0;i<s_count;i++)if((!s_filter||s_players[i].position==s_filter) && s_players[i].valueK>=s_priceBands[s_min] && s_players[i].valueK<=s_priceBands[s_max])s_filtered[s_visible++]=i;
    qsort(s_filtered,s_visible,sizeof(s_filtered[0]),compar_player);
    if(s_page*SEARCH_ROWS>=s_visible)s_page=0;
    int total=(s_visible+SEARCH_ROWS-1)/SEARCH_ROWS;if(!total)total=1;
    snprintf(s_filterText,sizeof(s_filterText),"POSITION: %s",s_posLabel[s_filter]);
    snprintf(s_minText,sizeof(s_minText),"MIN: %uK",s_priceBands[s_min]);
    snprintf(s_maxText,sizeof(s_maxText),"MAX: %s",s_max==10?"ANY":"LIMIT");
    if(s_max!=10)snprintf(s_maxText,sizeof(s_maxText),"MAX: %uK",s_priceBands[s_max]);
    snprintf(s_pageText,sizeof(s_pageText),"%d MATCHES  %d/%d",s_visible,s_page+1,total);
    for(int row=0;row<SEARCH_ROWS;row++){
        int at=s_page*SEARCH_ROWS+row;
        if(at>=s_visible){s_rows[row][0]=0;continue;}
        SearchPlayer *p=&s_players[s_filtered[at]];
        char val[16];
        if(p->valueK>=1000)snprintf(val,sizeof(val),"%u.%uM",(unsigned)p->valueK/1000,(unsigned)(p->valueK%1000)/100);
        else snprintf(val,sizeof(val),"%uK",(unsigned)p->valueK);
        /* Left-aligned name and age in one field. Keep the overall row width
         * identical to the original so the right-hand value stays in place. */
        char namedAge[48];
        /* The SWOS row font is proportional, so spaces alone do not
         * guarantee exact columns. Left-align the *menu entry itself* and
         * use concise fixed-width fields within its 288px row. */
        snprintf(namedAge,sizeof(namedAge),"%.16s (%u)",
                 p->name, (unsigned)p->age);
        snprintf(s_rows[row],sizeof(s_rows[row]),"%-21.21s %-10.10s %-3s %6s",
                 namedAge,p->club,s_posLabel[p->position],val);
        if (row == 0) search_log("ROW_DISPLAY team=%04X slot=%u id_age=%u formatted=%s",
                                 (unsigned)p->team, (unsigned)p->fileIndex,
                                 (unsigned)p->age, s_rows[row]);
    }
}
void WorldPlayerSearch(void){
    /* Close a stale handle defensively; this entrypoint is synchronous. */
    search_log_close();
    s_log = fopen("world_player_search.log", "w");
    if (s_log) setvbuf(s_log, NULL, _IONBF, 0);
    search_log("SEARCH ENTER log=world_player_search.log");
    scan_world();
    search_log("SCAN RETURNED count=%d", s_count);
    s_filter=0;s_min=0;s_max=10;s_page=0;
    /* Packed menu header stores the default selected entry as a 1-based
     * ordinal. ShowMenu unpacks it on every reopening, so remember the
     * control which caused a refresh instead of always selecting row 1. */
    uint32_t focusEntry = SEARCH_FIRST_ROW_ENTRY;
    for(;;){
        search_log("BUILD_LIST BEGIN filter=%d min=%d max=%d page=%d", s_filter, s_min,s_max,s_page);
        build_list();
        search_log("BUILD_LIST END visible=%d", s_visible);
        s_action=0;
        s_menu.hdr.defaultEntry = focusEntry;
        search_log("SHOW_MENU ENTER focus=%u", (unsigned)focusEntry);
        ShowMenu((uint8_t*)&s_menu);
        search_log("SHOW_MENU RETURN action=%d selected=%d", s_action, s_selected);
        if(s_action==6 || !s_action)break;
        if(s_action==2){s_filter=(s_filter+1)%SEARCH_POSITION_FILTERS;s_page=0;focusEntry=SEARCH_FILTER_ENTRY;continue;}
        if(s_action==3){s_min=(s_min+1)%11;if(s_min>s_max)s_max=s_min;s_page=0;focusEntry=SEARCH_MIN_ENTRY;continue;}
        if(s_action==7){s_max=(s_max+1)%11;if(s_max<s_min)s_min=s_max;s_page=0;focusEntry=SEARCH_MAX_ENTRY;continue;}
        if(s_action==4){if(s_page>0)s_page--;focusEntry=SEARCH_PREV_ENTRY;continue;}
        if(s_action==5){if((s_page+1)*SEARCH_ROWS<s_visible)s_page++;focusEntry=SEARCH_NEXT_ENTRY;continue;}
        if(s_action==1){
            /* Same offer dialog as the ordinary foreign/domestic market.
             * The worldwide index retains the actual team ID and physical
             * squad slot, which the offer routine expects. */
            int at=s_page*SEARCH_ROWS+s_selected;
            focusEntry=SEARCH_FIRST_ROW_ENTRY+s_selected;
            if(at>=0 && at<s_visible){
                SearchPlayer selected=s_players[s_filtered[at]];
                /* Guard the purchase entry point as well as the search scan. */
                if (selected.team == _selTeamNumber) {
                    search_log("BLOCK SELF-PURCHASE team=%04X slot=%u",
                               (unsigned)selected.team, (unsigned)selected.fileIndex);
                    scan_world();
                    s_page=0;
                    continue;
                }
                search_log("OFFER team=%04x ordinal=%u name=%s",
                           (unsigned)selected.team,(unsigned)selected.fileIndex,selected.name);
                ShowPlayerOfferMenuForPlayer(selected.team,selected.fileIndex);
                if(_transferCompleted){
                    /* A completed transfer changes the career roster. Do not
                     * show stale search entries when returning to this list. */
                    scan_world();
                    s_page=0;
                    _transferCompleted=0;
                }
            }
            continue;
        }
    }
    search_log_close();
}
