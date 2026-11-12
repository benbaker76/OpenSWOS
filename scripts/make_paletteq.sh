#!/usr/bin/env bash
# make_paletteq.sh -- the paletteq mod's lookup strips and presets (bin/mods/paletteq/shaders):
# for each palette its luts/<name>.png (scripts/palette_to_lut.py) and paletteq-<name>.slangp.
# Rerun after adding a palette here (and to src/mod_paletteq.c's table and mod.json's choices).
set -e
cd "$(dirname "$0")/.."
D=bin/mods/paletteq/shaders
mkdir -p "$D/luts"
# mod name : palette_to_lut.py's builtin : levels [: metric]. By CIEDE2000 (--metric lab) unless
# a metric is given: the
# weighted RGB distance kept brightness and lost hue -- the NES and PICO-8 pitches came out brown
# and orange. CGA keeps the weighted one: by hue its green pitch is white, by brightness it is
# the cyan-and-magenta dither CGA games are remembered by. (amiga-ecs: exact by any measure.)
PALETTES="
amiga-ecs:amiga-ecs:16
amiga-aga:swos-aga:32
dos-vga:vga-mode13h:32
c64:c64-colodore:32
nes:nes:32
master-system:master-system:32
mega-drive:mega-drive:32
game-boy:gameboy:32
virtual-boy:virtual-boy:32
atari-secam:atari-secam:32
amstrad-cpc:amstrad-cpc:32
zx-spectrum:zx-spectrum:32
msx:msx:32
intellivision:intellivision:32
super-cassette-vision:super-cassette-vision:32
odyssey2:odyssey2:32
cga:cga:32:weighted
pico-8:pico-8:32
"
for p in $PALETTES; do
    IFS=: read -r name builtin levels metric <<< "$p"
    python3 scripts/palette_to_lut.py --builtin "$builtin" "$D/luts/$name.png" --levels "$levels" --metric "${metric:-lab}" &
    cat > "$D/paletteq-$name.slangp" <<P
# The picture in the $name palette (paletteq.slang; luts/$name.png: scripts/make_paletteq.sh).
shaders = 1
shader0 = paletteq.slang
filter_linear0 = false
scale_type0 = viewport
textures = PALETTE_LUT
PALETTE_LUT = luts/$name.png
PALETTE_LUT_linear = false
P
done
wait
