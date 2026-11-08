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
                    // z: 1 = an RGBA source, no palette; w: key + 1 -- that index transparent
                    // (the menus' true-colour backdrop shows through), 0 none
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
    if (mode.w > 0.5 && i == int(mode.w + 0.5) - 1)
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
    if ((mode.y > 0.5 || mode.w > 0.5) && c.a <= 0.0)
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

//------------------------------------------------------------------------------
// crt: the finished picture (everything drawn in its space: the frame, the HUD, the mods'
// overlays) seen on a television, from an offscreen image the picture's size
// (gpu_present.c; [video] crt_curvature, crt_ntsc -- docs/RENDERING.md "The CRT pass").
//
//   CURVATURE: the tube's glass bends the picture -- each point pushed out along its distance
//   from the centre (a barrel), the edge bowing, outside the bent picture black.
//
//   NTSC: OpenSWOS's own model of a composite signal, written for this from how one works (not
//   any filter's code): the picture is ENCODED as a television carries it -- brightness Y plus
//   colour (I, Q) on a subcarrier whose phase runs two source pixels a cycle, turning half a cycle
//   each line and each frame -- and DECODED with a set's bandwidth: Y the signal averaged over
//   one subcarrier cycle (what is left of the colour at a sharp edge is the dot crawl), I and Q
//   the signal times the subcarrier, low-passed wider (Q wider still). So colours bleed sideways,
//   a sharp edge fringes, and fine detail shimmers. `ntsc` blends it with the clean picture.
//------------------------------------------------------------------------------

@vs vs_crt
layout(binding=0) uniform vs_crt_params {
    vec4 dst;       // the quad on the target, in clip space: x0, y0, x1, y1
};
out vec2 c;         // 0..1 over the picture
void main() {
    vec2 k = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(mix(dst.xy, dst.zw, k), 0.0, 1.0);
    c = k;
}
@end

@fs fs_crt
layout(binding=0) uniform texture2D pic_tex;    // the picture, RGBA8, the picture's size on screen
layout(binding=0) uniform sampler pic_smp;      // linear
layout(binding=1) uniform fs_crt_params {
    vec4 src;       // x, y: the game's picture in its own pixels (336 x 272); z: frame parity
    vec4 crt;       // x: curvature 0..1; y: ntsc 0..1; z: the image's y is flipped (1) or not
};
in vec2 c;
out vec4 frag_color;

const float PI = 3.14159265;

vec3 pic(vec2 t) {
    t.y = crt.z > 0.5 ? 1.0 - t.y : t.y;
    return texture(sampler2D(pic_tex, pic_smp), t).rgb;
}

vec3 rgb2yiq(vec3 c) {
    return vec3(dot(c, vec3(0.299, 0.587, 0.114)),
                dot(c, vec3(0.596, -0.274, -0.322)),
                dot(c, vec3(0.211, -0.523, 0.312)));
}

vec3 yiq2rgb(vec3 y) {
    return vec3(y.x + 0.956 * y.y + 0.621 * y.z,
                y.x - 0.272 * y.y - 0.647 * y.z,
                y.x - 1.106 * y.y + 1.703 * y.z);
}

// The composite picture at t (0..1). The subcarrier runs a cycle each two source pixels and the
// taps are a quarter pixel apart, so 8 taps are one cycle. Each filter is a whole number of cycles
// (the ends at half weight), which cancels the carrier exactly: a flat colour comes back as it was,
// and only edges and fine detail fringe. Y: one cycle; I: two; Q: three (the narrower band).
vec3 ntsc(vec2 t) {
    const int TAPS = 12;                // each side: Q's three cycles
    float px = 1.0 / src.x;             // one source pixel, in t
    float line = floor(t.y * src.y);
    float phase0 = PI * (line + src.z); // half a cycle a line, and a frame
    float u0 = t.x * src.x;             // in source pixels
    float ys = 0.0, is = 0.0, qs = 0.0;
    for (int k = -TAPS; k <= TAPS; k++) {
        float d = float(k) * 0.25;
        float ph = PI * (u0 + d) + phase0;
        vec3 s3 = rgb2yiq(pic(vec2(t.x + d * px, t.y)));
        float s = s3.x + s3.y * cos(ph) + s3.z * sin(ph);   // the composite signal
        int a = k < 0 ? -k : k;
        float wy = a < 4 ? 1.0 : a == 4 ? 0.5 : 0.0;
        float wi = a < 8 ? 1.0 : a == 8 ? 0.5 : 0.0;
        float wq = a < 12 ? 1.0 : 0.5;
        ys += s * wy;
        is += 2.0 * s * cos(ph) * wi;
        qs += 2.0 * s * sin(ph) * wq;
    }
    return clamp(yiq2rgb(vec3(ys / 8.0, is / 16.0, qs / 24.0)), 0.0, 1.0);
}

void main() {
    vec2 t = c;
    if (crt.x > 0.0) {
        vec2 p = t * 2.0 - 1.0;
        p *= 1.0 + crt.x * 0.12 * (p.yx * p.yx);    // the barrel
        t = p * 0.5 + 0.5;
        if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) {
            frag_color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    vec3 col = pic(t);
    if (crt.y > 0.0)
        col = mix(col, ntsc(t), crt.y);
    frag_color = vec4(col, 1.0);
}
@end

@program crt vs_crt fs_crt
