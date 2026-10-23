/*
 * sim_state.h -- the match state, as a registry of named variables.
 *
 * Everything a match needs to be rolled back to an earlier frame: save these
 * regions, let the game run on, load them, and the game continues exactly as it
 * did from the save. Netplay's GGPO save/load callbacks are built on it
 * (docs/NETPLAY.md), and scripts/rollback_test.sh proves the list by rolling real
 * matches back (docs/ROLLBACK.md, src/rollback_rig.c).
 *
 * Two tiers:
 *
 *   SIM_TIER_SIM   what the match reads, and everything that decides anything it
 *                  reads. Closed: no SIM byte depends on a VIEW byte, so SIM alone
 *                  is a complete resync and what a desync checksum covers. That
 *                  puts the draw list and every sprite in it here, HUD included:
 *                  a render pass sets `onScreen` from the list, and the match reads it.
 *   SIM_TIER_VIEW  the highlight ring and its pointers. Only instant-replay playback
 *                  reads it, but a local rollback that skips it leaves a rolled-back
 *                  goal in the replay.
 *
 * Render state that describes what is ALREADY DRAWN (scroll offsets, the
 * frontbuffers, the render stacks) is in neither tier on purpose: rolling it back
 * would make the renderer believe the screen shows an older frame than it does.
 *
 * Two encodings:
 *
 *   SimState_Save/Load        this build's memory, byte for byte: a rollback inside
 *                             this process, and nothing else.
 *   SimState_SavePortable/..  the PORTABLE encoding, the same bytes in every build --
 *                             32- or 64-bit, Linux, Windows or macOS -- the way saves
 *                             work: every region is a sequence of typed fields
 *                             (SimLayout) written big-endian, and every pointer is
 *                             written as an IDENTITY of what it points at (which
 *                             sprite, which team, which animation table), never as an
 *                             address. SimState_Hash covers the portable encoding, so
 *                             peers on different platforms compute the same checksum.
 */
#ifndef SIM_STATE_H
#define SIM_STATE_H

#include "swos_types.h"

/* SimStateRegion.flags */
#define SIM_VIEW         0x0002u   /* SIM_TIER_VIEW; otherwise SIM_TIER_SIM */
#define SIM_PART         0x0004u   /* covers part of its variable, not all of it */

/* Tier masks. */
#define SIM_TIER_SIM     0x0001u
#define SIM_TIER_VIEW    0x0002u
#define SIM_TIER_ALL     (SIM_TIER_SIM | SIM_TIER_VIEW)

/* SimField.type: how one run of an element travels in the portable encoding. */
typedef enum {
    SIM_FIELD_BYTES,        /* `count` bytes, copied */
    SIM_FIELD_WORD16,       /* `count` 16-bit values, big-endian */
    SIM_FIELD_WORD32,       /* `count` 32-bit values (int32, FixedPoint), big-endian */
    SIM_FIELD_POINTER,      /* one native pointer, as a 32-bit identity of `kind` */
    SIM_FIELD_TILE_SLOT,    /* one 32-bit pitch tile slot, as its tile's index */
    /* `count` 16- or 32-bit values that belong to THIS machine's display, not to the match:
     * saved, loaded and sent like WORD16/WORD32, but counted as zero in SimState_Hash -- the
     * checksum peers compare -- because two peers on different screens hold different
     * values. The x of a HUD sprite, placed for this display's width (docs/NETPLAY.md,
     * "One simulation width"). */
    SIM_FIELD_LOCAL16,
    SIM_FIELD_LOCAL32
} SimFieldType;

/* SimField.kind for SIM_FIELD_POINTER: what the pointer points into. A pointer's
 * identity is its target object's position in that kind's list and the element it
 * points at inside it; 0 is the null pointer and all-ones the -1 sentinel. */
enum {
    SIM_PTR_SPRITE = 1,         /* a Sprite: its slot in _teamSpritesTable */
    SIM_PTR_TEAM_INFO,          /* _topTeamData, _bottomTeamData */
    SIM_PTR_TEAM_GAME,          /* _topTeamInGame, _bottomTeamInGame */
    SIM_PTR_TEAM_STATS,         /* _team1StatsData, _team2StatsData */
    SIM_PTR_SPRITES_TABLE,      /* an element of _teamSpritesTable */
    SIM_PTR_SHOT_CHANCE,        /* _teamPlOfs24Table, then _kGoalieSkillTables[0..7] */
    SIM_PTR_ANIM_TABLE,         /* an animation table (src/sim_state_ids.h) */
    SIM_PTR_FRAME_INDICES,      /* a frame-index array, or a word in one (sim_state_ids.h) */
    SIM_PTR_GOAL_RING,          /* a byte of the highlight ring _goalBase, or its end */
    SIM_PTR_TACTICS,            /* a byte of a _teamTacticsTable record */
    SIM_PTR_KINDS
};

typedef struct {
    uint16_t offset;        /* where the run starts in this build's element */
    uint16_t count;
    uint8_t  type;          /* SimFieldType */
    uint8_t  kind;          /* SIM_PTR_* for SIM_FIELD_POINTER */
} SimField;

/* One element of a region: `stride` bytes in this build's memory, tiled exactly by
 * `fields` in order (SimState_LayoutError checks it). */
typedef struct {
    uint32_t        stride;
    uint32_t        fieldCount;
    const SimField *fields;
} SimLayout;

typedef struct {
    const char      *name;
    void            *addr;
    uint32_t         size;          /* bytes in this build's memory */
    uint32_t         flags;
    const SimLayout *layout;
} SimStateRegion;

extern const SimStateRegion g_simStateRegions[];
extern const int32_t        g_simStateRegionCount;

/* The tier mask a region belongs to. */
#define SIM_REGION_TIER(r) (((r)->flags & SIM_VIEW) ? SIM_TIER_VIEW : SIM_TIER_SIM)

/* ---- this build's memory: a rollback in this process ---- */

uint32_t SimState_Size(uint32_t tiers);
void     SimState_Save(uint8_t *dst, uint32_t tiers);
/* Also brings derived render mirrors back in line with what it loaded. */
void     SimState_Load(const uint8_t *src, uint32_t tiers);
/* From a SimState_Save buffer, put back only the LOCAL fields (this display's): after
 * loading another peer's state, the match is theirs and the HUD's placement ours. */
void     SimState_RestoreLocal(const uint8_t *src, uint32_t tiers);

/* ---- the portable encoding: the same in every build ---- */

uint32_t SimState_PortableSize(uint32_t tiers);
uint32_t SimState_RegionPortableSize(int32_t index);

/* Returns 0, or how many pointers had no identity -- they are written as a reserved
 * value that no load accepts, so such a buffer must not be sent. */
int      SimState_SavePortable(uint8_t *dst, uint32_t tiers);

/* Returns 0, or -1 if the buffer names something this build cannot resolve; then
 * nothing is loaded. */
int      SimState_LoadPortable(const uint8_t *src, uint32_t tiers);

/* FNV-1a over the portable encoding of these tiers: equal on every platform for the
 * same match state. */
uint64_t SimState_Hash(uint32_t tiers);

/* Pointers the last SimState_Hash found no identity for. Anything but 0 means the
 * checksum cannot be trusted between builds. */
uint32_t SimState_HashUnidentified(void);

/* A fingerprint of what a portable buffer means: every region's name, portable size,
 * tier, field layout and whether it is disabled, and the shape of the identity lists.
 * Nothing about this build's memory -- so equal between builds of the same source. */
uint64_t SimState_LayoutHash(void);

/* The identity of any pointer the match state holds, whatever its kind: returns 1 with
 * the kind and the identity word, or 0 if nothing it knows contains it. */
int      SimState_PointerIdentity(const void *p, uint32_t *kind, uint32_t *identity);

/* How many objects a pointer of `kind` (SIM_PTR_*) can point into in this build, right
 * now. Diagnostic: a kind whose list is EMPTY gives every pointer of that kind no
 * identity, which is how a target that cannot save its state says so. */
uint32_t SimState_IdentityListCount(uint32_t kind);

/* The SIM_PTR_* kind, and the value, of the last pointer that found no identity. */
uint32_t SimState_LastUnidentifiedKind(void);
uint32_t SimState_LastUnidentifiedPtr(void);
uint32_t SimState_UnidentifiedOfKind(uint32_t kind);
void     SimState_ClearUnidentified(void);

/* Which field a differing byte of the PORTABLE encoding belongs to: writes something like
 * "[3 of 40] +42 word16" -- the element, and the NATIVE offset, which is what the struct in
 * game_types.h names. Returns 0 (and "byte N") when the region has no usable layout. */
#ifdef SWOS_SDL_BUILD
int      SimState_DescribePortableByte(int32_t index, uint32_t offset, char *out, uint32_t outLen);
/* Which bytes of region `index`'s portable encoding are LOCAL fields (1 = local): zero in
 * the hash peers compare, so a diff of two peers should skip them too. `mask` must hold
 * SimState_RegionPortableSize(index) bytes. */
void     SimState_PortableLocalMask(int32_t index, uint8_t *mask);
#endif

/* NULL, or why region `index`'s layout does not describe it (fields that do not tile
 * the element exactly, a region that is not a whole number of elements). */
const char *SimState_LayoutError(int32_t index);

/* Whether region `index` takes part in the calls above for these tiers. */
int      SimState_RegionActive(int32_t index, uint32_t tiers);

/* Test hook for the rollback rig: leave a region out of every call above, to try a
 * removal without rebuilding. Returns 0 when no region has that name. */
int      SimState_Disable(const char *name);

#endif /* SIM_STATE_H */
