/*
 * rnc_archive.h -- Rob Northen's archive format, "RNCA": many files, each RNC-packed (method 1
 * or 2) or stored (method 0), behind a folder list. The PC game's floppy release is one of
 * these, split across its two disks (SWOS_1.RNC, SWOS_2.RNC: the halves joined are the archive).
 *
 *   000h "RNCA", 004h end of the folder list (BE16), 006h CRC-16 of the folder list (BE16, from
 *   008h), 008h the folders: per folder the end of its file list (BE16), its path ("SFX\11\",
 *   "" for the top, NUL-ended), per file its name (NUL-ended) and its data's offset (BE32), and
 *   a 00h; then the files' data, each with its RNC header ("RNC" + method; method 0's header is
 *   8 bytes, "RNC\0" + its size), and four 00h at the end.
 *   (psx-spx, "CDROM File Compression RNC": problemkaputt.de)
 */
#ifndef SWOSDATA_RNC_ARCHIVE_H
#define SWOSDATA_RNC_ARCHIVE_H

#include <stdint.h>

/* Whether a buffer starts as an RNC archive. */
int32_t RncArchive_Is(const uint8_t *data, uint32_t size);

/* Every file in the archive unpacked under `dest` (folders made, '\' as '/'); how many in
 * *files. 0 on success; a CRC that does not match, or anything out of bounds, is -1. */
int32_t RncArchive_Extract(const uint8_t *data, uint32_t size, const char *dest, int32_t *files, char *err);

#endif
