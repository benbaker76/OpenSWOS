#version 450
/*
 * paletteq.slang -- the picture in an old machine's colours (the paletteq mod, bin/mods/paletteq),
 * in RetroArch's slang. OpenSWOS's own, GPL 3; the method is Jungool's C64 mode (Ben Baker).
 *
 * Each game pixel is taken whole (the pass draws at the window's size, but every screen pixel looks
 * up the one game pixel it lies in, so the blocks stay hard-edged), nudged by a 4x4 ordered (Bayer)
 * dither fixed to the game's pixel grid -- a scrolling pitch keeps its pattern rather than
 * crawling, as the old machines' own games did -- and turned into the palette's nearest colour by
 * one read of PALETTE_LUT: an N x N x N strip (N tiles of N x N side by side: red across a tile,
 * green down it, blue picks the tile) made by scripts/palette_to_lut.py. N is the strip's height.
 *
 * DITHER: the dither's strength, in colour (0 none, 0.25 a quarter of the range).
 * GRID_X, GRID_Y: where the dither's grid starts, in game pixels, 0..3 -- the mod sets them each
 * frame from the camera, so the pattern is fixed to the PITCH and scrolls with it (fixed to the
 * screen, a scrolling pitch swam under a still pattern).
 */
#pragma parameter DITHER "Dither strength" 0.12 0.0 0.5 0.01
#pragma parameter GRID_X "Dither grid x" 0.0 0.0 3.0 1.0
#pragma parameter GRID_Y "Dither grid y" 0.0 0.0 3.0 1.0

layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 OutputSize;
    uint FrameCount;
    float DITHER;
    float GRID_X;
    float GRID_Y;
} params;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
layout(set = 0, binding = 3) uniform sampler2D PALETTE_LUT;

/* 4x4 Bayer thresholds, 0..15. */
const float bayer4[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

void main()
{
    /* the game pixel this screen pixel lies in, and its colour at its centre */
    vec2 px = floor(vTexCoord * params.SourceSize.xy);
    vec3 colour = texture(Source, (px + 0.5) * params.SourceSize.zw).rgb;

    ivec2 b = ivec2(mod(px + vec2(params.GRID_X, params.GRID_Y), 4.0));
    colour += ((bayer4[b.y * 4 + b.x] + 0.5) / 16.0 - 0.5) * params.DITHER;

    float n = float(textureSize(PALETTE_LUT, 0).y);
    vec3 level = floor(clamp(colour, 0.0, 1.0) * (n - 1.0) + 0.5);
    vec2 uv = vec2((level.b * n + level.r + 0.5) / (n * n), (level.g + 0.5) / n);
    FragColor = vec4(texture(PALETTE_LUT, uv).rgb, 1.0);
}
