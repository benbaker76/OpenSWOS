//------------------------------------------------------------------------------
// present.glsl -- the GPU presenter's shaders (src/gpu_present.c, docs/RENDERING.md).
// Compiled by scripts/gen_shaders.sh (sokol-shdc) into present.glsl.h, committed.
//
// present: the game's picture. Its 8bpp frame (palette indices, an R8 texture) is coloured
//   through the palette (256x1 RGBA8) -- or, for the arcade screens that are drawn in RGBA, read
//   as an RGBA texture -- and scaled: nearest, or "smooth" = nearest to a whole factor n and
//   linear from there, exactly what the SDL path's two passes do (platform_sdl.c
//   present_frame_copy), done here in one. The sub-pixel scroll moves the source rectangle, the
//   last row and column clamped as the SDL path's slivers are. The HUD layer is the same
//   program over its own R8 texture: index 0 is transparent, the rest premultiplied.
// batch: the overlays' triangles (gfx.h: the weather, the flags, the CRT mask, the touch
//   controls) -- a position in pixels, a colour and a texture coordinate.
//------------------------------------------------------------------------------

@vs vs_present
layout(binding=0) uniform vs_present_params {
    vec4 dst;       // the quad on the target, in clip space: x0, y0, x1, y1
    vec4 src;       // the source rectangle, in source pixels: u0, v0, u1, v1
};
out vec2 uv;        // in source pixels
void main() {
    // A strip of four: (0,0) (1,0) (0,1) (1,1).
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(mix(dst.xy, dst.zw, c), 0.0, 1.0);
    uv = mix(src.xy, src.zw, c);
}
@end

@fs fs_present
layout(binding=0) uniform texture2D idx_tex;    // R8 palette indices (or RGBA8, mode.z)
layout(binding=0) uniform sampler idx_smp;      // nearest: every read is a texelFetch anyway
layout(binding=1) uniform texture2D pal_tex;    // 256x1 RGBA8
layout(binding=1) uniform sampler pal_smp;
layout(binding=1) uniform fs_present_params {
    vec4 size;      // the source's width, height
    vec4 mode;      // x: the smooth factor n (0: nearest); y: 1 = the HUD (index 0 transparent);
                    // z: 1 = an RGBA source, no palette
};
in vec2 uv;
out vec4 frag_color;

// One source pixel, clamped to the picture, premultiplied by its alpha.
vec4 texel(vec2 t) {
    ivec2 p = ivec2(clamp(t, vec2(0.0), size.xy - vec2(1.0)));
    vec4 s = texelFetch(sampler2D(idx_tex, idx_smp), p, 0);
    if (mode.z > 0.5)
        return vec4(s.rgb, 1.0);
    int i = int(s.r * 255.0 + 0.5);
    if (mode.y > 0.5 && i == 0)
        return vec4(0.0);
    vec4 c = texelFetch(sampler2D(pal_tex, pal_smp), ivec2(i, 0), 0);
    return vec4(c.rgb, 1.0);
}

void main() {
    vec4 c;
    if (mode.x < 0.5) {
        c = texel(floor(uv));
    } else {
        // Smooth: the picture scaled n times by nearest, then sampled linearly -- four reads
        // of the n-times picture around this point, each its source pixel.
        float n = mode.x;
        vec2 q = uv * n - vec2(0.5);
        vec2 b = floor(q);
        vec2 f = q - b;
        vec4 c00 = texel(floor((b + vec2(0.5, 0.5)) / n));
        vec4 c10 = texel(floor((b + vec2(1.5, 0.5)) / n));
        vec4 c01 = texel(floor((b + vec2(0.5, 1.5)) / n));
        vec4 c11 = texel(floor((b + vec2(1.5, 1.5)) / n));
        c = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    }
    if (mode.y > 0.5 && c.a <= 0.0)
        discard;
    frag_color = c;
}
@end

@program present vs_present fs_present

//------------------------------------------------------------------------------

@vs vs_batch
layout(binding=0) uniform vs_batch_params {
    vec4 view;      // the target's width, height in pixels (y down)
};
in vec2 pos;
in vec4 color0;
in vec2 texcoord0;
out vec4 color;
out vec2 tc;
void main() {
    gl_Position = vec4(pos.x / view.x * 2.0 - 1.0, 1.0 - pos.y / view.y * 2.0, 0.0, 1.0);
    color = color0;
    tc = texcoord0;
}
@end

@fs fs_batch
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec4 color;
in vec2 tc;
out vec4 frag_color;
void main() {
    frag_color = texture(sampler2D(tex, smp), tc) * color;
}
@end

@program batch vs_batch fs_batch
