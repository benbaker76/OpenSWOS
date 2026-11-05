# pitch_weather in OpenSWOS

**Source:** github.com/C4ppy77/pitch-weather-SWOS, `src/pitchWeather.h` / `src/pitchWeather.cpp` at
`9e67344` (2026-10-19). **Licence:** MIT, Copyright (c) 2026 C4ppy77 (`LICENSE`, kept as it is).

**What changed:** ported from C++ to C (`pitch_weather.c` / `.h`) so it builds with every SDL target
of the game, several of which have no C++ in their build: `std::vector` became fixed arrays (sized
for the most drops any weather draws, 210), the namespace a `PitchWeather_` prefix, the lambda a
function. The weather itself -- every constant, wave, colour and the drawing -- is the same.

**How OpenSWOS uses it:** the canonical mod `weather` (`bin/mods/weather/mod.json`, off by default,
OPTIONS -> MODS; code in `src/mod_weather.c`, compiled by `src/pitch_weather_impl.c`) draws it over
every match frame, from the match's pitch type and minute (docs/MODS.md §16).

**Updating:** port the upstream change into `pitch_weather.c` by hand, keep this file's commit line.
