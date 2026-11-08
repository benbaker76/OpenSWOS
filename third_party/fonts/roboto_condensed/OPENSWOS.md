# Roboto Condensed in OpenSWOS

`RobotoCondensed-Regular.ttf`, version 2.137 (2017). Copyright 2011 Google Inc., licensed under
the Apache License, Version 2.0 (`LICENSE.txt`), as the font's own `name` table says.

The chat (`src/chat_font.c`, `src/match_chat.c`) draws in it. The game does not read this file:
`scripts/make_chat_font.sh` subsets it to the characters the chat accepts -- Basic Latin, Latin-1,
Latin Extended-A (the European languages), dashes, curly quotes, the bullet, the ellipsis and the
euro sign -- with its kerning, no hinting, and writes the subset into `src/chat_font_data.c` as a
C array (about 25 KB), so no drop has to ship a font file and no build needs the tools.

Rerun the script after changing the font or the character set (it needs Python's fonttools:
`pip install fonttools`). Add to the character set in `chat_font.c` (`kRanges`) at the same time.
