#ifndef DIY_IMPORT_DOS_H
#define DIY_IMPORT_DOS_H

#include <stdint.h>

/* A .DIY written by the PC game or SWOS 2020 (a raw dump of the DOS competitionFileBuffer
 * region plus its teams), loaded into the same state a JSON .DIY load leaves. */

/* 1 when the file is laid out as a DOS .DIY (its size, numbers and type agree). */
int32_t DiyImportDos_IsDosFile(const uint8_t *data, uint32_t size);

/* 0 = loaded, 1 = not a DOS .DIY or not loadable on this target. */
int16_t DiyImportDos_Load(uint8_t *filename);

/* The same from a file already read; `label` names it in the log. keepSettings
 * leaves the player's six options and USER tactics alone instead of taking the
 * file's (a classic competition is started, not resumed). */
int16_t DiyImportDos_LoadData(const char *label, const uint8_t *data, uint32_t size,
                              int32_t keepSettings);

#endif
