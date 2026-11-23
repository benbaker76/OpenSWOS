/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef TEAM_COLORS_H
#define TEAM_COLORS_H

#include "game_types.h"

/* ---- Shirt types ---- */
#define SHIRT_ORDINARY          0
#define SHIRT_COLORED_SLEEVES   1
#define SHIRT_VERTICAL_STRIPES  2
#define SHIRT_HORIZONTAL_STRIPES 3

/* ---- Exported functions ---- */
uint16_t __attribute__((used, externally_visible)) ConvertTeamColor(uint16_t color);
int16_t  __attribute__((used, externally_visible)) AreBasicColorsConflicting(uint16_t color1, uint16_t color2);
int16_t  __attribute__((used, externally_visible)) AreShortsAndStripeColorsConflicting(uint16_t color1, uint16_t color2);
int16_t  __attribute__((used, externally_visible)) GetTeamKitsSimilarityFactor(
    uint16_t _shirtType1, uint8_t stripesColor1, uint8_t basicColor1, uint8_t shortsColor1,
    uint16_t _shirtType2, uint8_t stripesColor2, uint8_t basicColor2, uint8_t shortsColor2);
void     __attribute__((used, externally_visible)) MeasureTeamKitsSimilarity(TeamFile *team1, TeamFile *team2);
void     __attribute__((used, externally_visible)) ConvertTeamKitColors(
    uint16_t shirtType, uint8_t stripesColor, uint8_t basicColor, uint8_t shortsColor, uint8_t socksColor,
    TeamGame *team);
void     __attribute__((used, externally_visible)) SetTeamSecondaryColors(
    uint16_t shirtType, uint8_t stripesColor, uint8_t basicColor, uint8_t shortsColor, uint8_t socksColor,
    TeamGame *team);
void     __attribute__((used, externally_visible)) SetInGameTeamColors(uint16_t secondaryKitFlags, TeamFile *team1File, TeamFile *team2File);
void     __attribute__((used, externally_visible)) SetInGameTeamsPrimaryColors(TeamFile *team1File, TeamFile *team2File);
#ifdef SWOS_SDL_BUILD
void     SetInGameTeamsFirstKits(TeamFile *team1File, TeamFile *team2File);   /* chosen kits: no clash rule */
#endif
uint16_t __attribute__((used, externally_visible)) ConvertPlayerDressColor(uint16_t color);

/* Purple, the 11th kit colour (team_colors.c): the menu entry at start-up, the match's palette
 * entry 12 before its sprites are coloured, a side's kit colours as last given (type, stripes,
 * basic, shorts, socks; 0 for another TeamGame). */
#define KIT_COLOURS 11
void TeamColors_Init(void);
/* The kit variants' entries in the menu palette (128 on): once the intro's pictures are gone. */
void TeamColors_MenuVariants(void);
void TeamColors_MatchPalette(TeamGame *team1);
int32_t TeamColors_PurpleInMatch(void);   /* a side wears purple and neither maroon */
#ifdef SWOS_SDL_BUILD
/* After a match's sprites are coloured: while purple has entry 12, the match's own maroon art
 * (banners, corner flags, the red card ...) drawn with entry 4 instead (report 0056). */
void TeamColors_MatchArt(void);
void TeamColors_MatchArtEnd(void);   /* at the match's end: the art put back */
#endif
/* The kit bytes a side wears (type, stripes, basic, shorts, socks), or NULL when the in-game
 * team was not made from them (a replay's, another match's). */
const uint8_t *TeamColors_Worn(const TeamGame *team);
void TeamColors_SetWorn(const TeamGame *team, const uint8_t kit[5]);
/* The entry a part of a side's kit (0 shirt, 1 second colour, 2 shorts, 3 socks) is drawn in: `base`,
 * or a variant's own -- in the match palette, or with `menu` in the menu palette. */
uint16_t TeamColors_PartEntry(const TeamGame *team, int32_t part, uint16_t base, int32_t menu);
/* A kit colour byte's entry in the menu palette (a variant's own with the EXTENDED rule). */
uint16_t TeamColors_MenuEntry(uint8_t colour);

#endif
