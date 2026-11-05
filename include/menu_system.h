/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include "swos_types.h"  /* uintptr_t for MenuEntry.bg / .fg dual-use slots */

/* Forward decl so callback typedefs below can take MenuEntry *. */
typedef struct MenuEntry MenuEntry;

/* Function-pointer typedefs for menu callbacks. */
typedef void (*MenuVoidFn)(void);              /* Menu.onInit / afterDraw / onDraw */
typedef void (*MenuEntryFn)(MenuEntry *entry); /* MenuEntry.onSelect / beforeDraw / afterDrawCb */

/* ---- Menu struct (22 bytes) ---- */
/* Packed to match 68k memory layout exactly. On 32-bit the function
 * pointer + struct pointer slots are 4 bytes each, preserving the
 * size_check. On 64-bit they grow to 8 bytes — Phase 5 / 6 retypes
 * the size_check accordingly. */
typedef struct __attribute__((packed)) {
    MenuVoidFn   onInit;       /* 0: function pointer (or NULL) */
    MenuVoidFn   afterDraw;    /* 4: function pointer (or NULL) */
    MenuVoidFn   onDraw;       /* 8: function pointer (or NULL) */
    MenuEntry   *selectedEntry;/* 12: pointer to selected MenuEntry */
    uint16_t     numEntries;   /* 16 */
    uint32_t     endOfMenuPtr; /* 18 — still uint32_t (raw addr in template) */
} Menu;

/* ---- MenuEntry struct (56 bytes) ---- */
struct __attribute__((packed)) MenuEntry {
    uint16_t alreadyDrawn;   /* 0 */
    uint16_t ordinal;        /* 2 */
    uint16_t isInvisible;    /* 4 */
    uint16_t isDisabled;     /* 6 */
    int8_t   leftEntry;      /* 8 */
    int8_t   rightEntry;     /* 9 */
    int8_t   upEntry;        /* 10 */
    int8_t   downEntry;      /* 11 */
    int8_t   directionLeft;  /* 12 */
    int8_t   directionRight; /* 13 */
    int8_t   directionUp;    /* 14 */
    int8_t   directionDown;  /* 15 */
    int8_t   skipLeftEntry;  /* 16 */
    int8_t   skipRightEntry; /* 17 */
    int8_t   skipUpEntry;    /* 18 */
    int8_t   skipDownEntry;  /* 19 */
    uint16_t x;              /* 20 */
    uint16_t y;              /* 22 */
    uint16_t dwWidth;        /* 24 */
    uint16_t dwHeight;       /* 26 */
    uint16_t background;     /* 28 */
    /* bg/fg are dual-use slots driven by `background` / `content` tags:
     *   - ENTRY_FUNC1 / ENTRY_FUNC2          → function pointer (`bg`/`fg`)
     *   - ENTRY_STRING / ENTRY_STRING_TABLE
     *     / ENTRY_MULTI_LINE_TEXT            → string pointer (`bg`/`fg`)
     *   - ENTRY_SPRITE1 / ENTRY_SPRITE2
     *     / ENTRY_SPRITE_COPY                → sprite ptr (`bg`/`fg`) OR
     *                                          sprite index (`bgWord`/`fgWord`)
     *   - ENTRY_FRAME_AND_BACK_COLOR         → color word (`bgColor`)
     *   - ENTRY_NUMBER                       → integer (`fgWord` / `fgNumber`)
     *
     * Anonymous union — the pointer alias (`bg`/`fg`, uintptr_t) and the
     * first-word alias (`bgColor`/`bgWord` and `fgWord`/`fgNumber`,
     * uint16_t) refer to the SAME bytes. The uint16 alias maps to the
     * FIRST 2 BYTES of the slot in memory order (= high 16 bits on BE
     * 32-bit Amiga, low 16 bits on LE).
     *
     * IMPORTANT: when storing a 16-bit value (color word / sprite
     * index / number) into the slot, ALWAYS write through the uint16
     * alias (`bgColor`, `bgWord`, `fgWord`, `fgNumber`, `fgSpriteIdx`).
     * Writing `bg = (uintptr_t)16bit_value` zero-extends to 32 bits and
     * on BE puts the value in the LAST 2 bytes — the reader then reads
     * 0 from `bgColor` (= first 2 bytes). Only use the uintptr_t alias
     * for actual 32-bit pointers (function pointers, string pointers).
     * Replaces the historical `(uint8_t *)e + 38` byte arithmetic. */
    union {
        uintptr_t bg;        /* 30: pointer / function-ptr / string ptr */
        uint16_t  bgColor;   /* first 2 bytes: color word for ENTRY_FRAME_AND_BACK_COLOR */
        uint16_t  bgWord;    /* alias of bgColor for sprite-index / number reads */
    };
    uint16_t content;        /* 34 (32-bit) / 38 (64-bit, bg grows to 8B) */
    uint16_t stringFlags;    /* 36 / 40 */
    union {
        uintptr_t fg;        /* 38 / 42: pointer / function-ptr / string ptr */
        uint16_t  fgWord;    /* first 2 bytes: integer payload */
        uint16_t  fgNumber;  /* alias of fgWord — clearer name at ENTRY_NUMBER sites */
        uint16_t  fgSpriteIdx; /* alias of fgWord — clearer at ENTRY_SPRITE2 sites */
    };
    MenuEntryFn onSelect;    /* 42 / 46: function pointer (or NULL) */
    uint16_t controlMask;    /* 46 / 54 */
    MenuEntryFn beforeDraw;  /* 48 / 56: function pointer (or NULL) */
    MenuEntryFn afterDrawCb; /* 52 / 64: function pointer (or NULL) */
};

/* Direct field access via the union aliases above — no helper layer.
 * Read/write the bg/fg first-word as `e->bgColor` / `e->fgWord`, and
 * read/write the full pointer payload as `e->bg` / `e->fg`. */

/* Compile-time size checks */
/* Menu has 4 pointer fields (onInit, afterDraw, onDraw, selectedEntry);
 * +16 on 64-bit. endOfMenuPtr is still uint32_t (raw offset in
 * template), so it doesn't contribute.
 * MenuEntry has 5 pointer-width fields (bg, fg uintptr_t + onSelect,
 * beforeDraw, afterDrawCb fn-ptrs); +20 on 64-bit. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _menu_size_check[(sizeof(Menu) == 22) ? 1 : -1];
typedef char _menuentry_size_check[(sizeof(MenuEntry) == 56) ? 1 : -1];
#else
typedef char _menu_size_check[(sizeof(Menu) == 38) ? 1 : -1];
typedef char _menuentry_size_check[(sizeof(MenuEntry) == 76) ? 1 : -1];
#endif

/* ---- Constants ----
 * Bitness-conditional because Menu has 4 pointer fields and MenuEntry has
 * 5 pointer-width fields (uintptr_t bg/fg + 3 fn-ptrs); both struct sizes
 * grow on 64-bit (+16 / +20). The `menuEntries` link-time alias in
 * swos_globals.c uses the same offset and must stay in sync. */
#if UINTPTR_MAX == 0xFFFFFFFFu
#define MENU_HEADER_SIZE   22
#define MENUENTRY_STRIDE   56
#else
#define MENU_HEADER_SIZE   38
#define MENUENTRY_STRIDE   76
#endif
#define NO_BACKGROUND      0
#define NO_FOREGROUND      0

/* OpenSWOS packed-menu extension (docs/MENUS.md §0a): an optional
 * `MENU_FLAGS_MARKER, u16 flags` pair directly after the menu header. */
#define MENU_FLAGS_MARKER     0xFC18
#define MENU_FLAG_DOS_REDRAW  0x0001   /* DOS/SWOS 2020 MenuProc: redraw the whole menu,
                                          onDraw included, every frame */

/* Background types (MENUENTRY_BACKGROUND field) */
#define ENTRY_FUNC1                 1
#define ENTRY_FRAME_AND_BACK_COLOR  2
#define ENTRY_SPRITE1               3

/* Content/foreground types (MENUENTRY_CONTENT field) */
#define ENTRY_FUNC2            1
#define ENTRY_STRING           2
#define ENTRY_SPRITE2          3
#define ENTRY_STRING_TABLE     4
#define ENTRY_MULTI_LINE_TEXT  5
#define ENTRY_NUMBER           6
#define ENTRY_SPRITE_COPY      7

/* String flags masks */
#define STR_COLOR_MASK   15
#define STR_FLAGS_MASK   240

/* Color constants */
#define YELLOW_GREEN_BLACK_15  15

/* CharTable offsets */
#define CHARTABLE_CHARHEIGHT       2
#define CHARTABLE_CHARSPACING      8

/* SpriteGraphics offsets */
#define SPRITEGRAPHICS_PIXWIDTH   10
#define SPRITEGRAPHICS_NLINES     12

/* ---- Asm helpers (save/restore all regs around callback) ---- */
extern void CallMenuCallback(void *funcPtr);
extern void CallMenuOnSelect(void *funcPtr, void *selectedEntry);
extern void CallMenuFuncWithXY(void *funcPtr, uint16_t x, uint16_t y);

/* ---- Ported functions ---- */
void MenuProc(void);
void ActivateMenu(uint8_t *packedMenu);
void ShowMenu(uint8_t *packedMenu);
void RestorePreviousMenu(MenuEntry *oldSelectedEntry, uint8_t *oldPackedMenu);
void DrawMenu(void);
void MenuCheckControls(void);
void DrawSelectedMenuItemFrame(void);
void UpdateBlinkingMenuText(void);
void DrawMenuPanelBackground(void);
void SetExitMenuFlag(void);
MenuEntry *CalcMenuEntryAddress(uint16_t ordinal);
void UnpackMenu(uint8_t *packed, uint8_t *dest);
void DrawMenuItem(MenuEntry *entry);
void CallDrawMenuItem(MenuEntry *entry);
void DrawMenuItemInnerFrame(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void DrawMenuItemOuterFrame(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void DrawMenuItemBackground(uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void DrawMenuItemBackgroundMasked(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void DrawMenuBackWithCheck(MenuEntry *entry);
void DrawBackAllItems(uint16_t maxItems, MenuEntry *firstEntry);
uint16_t GetKey(void);
void BlitterCopyOverlayToScreen(void);
void Int2Ascii(uint16_t value, uint8_t *out);
void SetCurrentEntry(uint16_t index);
/* The main menu is the one being shown (its buffer: a mod's replacement of it too). */
int32_t Menu_IsMainMenu(void);
void BlitMaskedRectAltToFront(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void DrawMenuItemsFromList(const int16_t *list);
void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
void DrawMenuItemsWaitForVSync(const int16_t *list);
void CallDrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);
void CallDrawMultipleItemsRect(uint16_t maxItems, MenuEntry *firstEntry);

#endif
