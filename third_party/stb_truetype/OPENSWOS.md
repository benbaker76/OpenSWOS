# stb_truetype in OpenSWOS

`stb_truetype.h` v1.26, public domain (or MIT, at your choice), by Sean Barrett:
https://github.com/nothings/stb, commit `f58f558c120e9b32c217290b80bad1a0729fbb2c`, unchanged.

Used by `src/chat_font.c` (SDL builds) to rasterize the chat's typeface at the window's own pixel
size. Its implementation is compiled there, `static`, so nothing else links against it.

To update: replace the file from a newer commit, note the commit here, and check the chat
(`--chat-selftest`) still draws.
