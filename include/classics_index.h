/*
 * classics_index.h -- a historic-tournaments folder's index for the Amiga (the classics mod,
 * src/mod_classics.c; docs/DATA_BSON.md: the Amiga parses no JSON). The PC reads the folder's
 * index.json (scripts/make_classics_index.py); OpenSWOS Setup writes this beside it when it puts
 * the mod in an Amiga drawer (SwosData_AmigaMod):
 *
 *   INDEX.BIN  "OSWCIDX" + a version byte (1), a u16 count (big-endian), then per competition
 *              its file[64] and name[36], zero-padded -- index.json's "file" and "name".
 */
#ifndef CLASSICS_INDEX_H
#define CLASSICS_INDEX_H

#define CLASSICS_INDEX_FILE    "INDEX.BIN"
#define CLASSICS_INDEX_MAGIC   "OSWCIDX"
#define CLASSICS_INDEX_VERSION 1
#define CLASSICS_INDEX_HEADER  10
#define CLASSICS_INDEX_FILELEN 64
#define CLASSICS_INDEX_NAMELEN 36
#define CLASSICS_INDEX_RECORD  (CLASSICS_INDEX_FILELEN + CLASSICS_INDEX_NAMELEN)

#endif /* CLASSICS_INDEX_H */
