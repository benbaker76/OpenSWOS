/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef FILE_IO_H
#define FILE_IO_H

#include "menu_system.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;

/* ---- File I/O functions ---- */
int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
int16_t WriteFile(uint8_t *filename, uint8_t *src, uint32_t _size);
void ShowFsErrorByCode(uint16_t errCode);
int16_t LoadCareerFile(uint8_t *filename);
int16_t SaveCareerFile(uint8_t *filename);
int16_t LoadPresetFile(uint8_t *filename);
int16_t SavePresetCompetitionFile(uint8_t *filename);
int16_t LoadDIYFile(uint8_t *filename);
int16_t SaveDIYFile(uint8_t *filename);
int16_t FindFilesExtensionPresent(uint8_t *ext);
int16_t ReadTacticsFile(uint8_t *filename);
int16_t WriteTacticsFile(uint8_t *filename, uint8_t *buffer);
int16_t LoadHilFile(uint8_t *filename);
int16_t DoSaveHighlights(uint8_t *filename);
void DiskMenuRet(void);
void ShowInsertDisk2Dialog(uint8_t *header);
int16_t Asset_PromptInsertSaveDiskDf0(uint8_t *title);
void FormatSaveDiskMenu(void);
void DeleteFileMenu(void);
void ExitDisk(void);
int16_t SelectFileToSaveDialog(uint32_t filterExt, uint8_t *defaultFilename, uint8_t *menuTitleStr);
int16_t GetFilenameAndExtension(uint32_t preferredExt, uint8_t *defaultFilename, uint8_t *menuTitleStr);
/* The same picker filtered to ONE extension (CHALLENGES; src/file_io.c). */
int16_t SelectFileOfExtension(uint32_t ext, uint8_t *menuTitleStr);

/* ---- File Selection Helpers (Chunk 140) ---- */
void SelectFilenameToSaveAndExit(void);
void AddExtension(uint8_t *filename);
void RepositionSelectFileEntries(void);

/* ---- SelectFilesInputSaveFilenameOnSelect (Chunk 142) ---- */
void SelectFilesInputSaveFilenameOnSelect(MenuEntry *entry);

/* --- Chunk 218: SelectFilesInit, SelectFilesOnReturn --- */
void SelectFilesInit(void);
void SelectFilesOnReturn(void);

#endif
