#version 450
/*
 * openswos-tv.slang -- OpenSWOS's television: the tube's glass and a composite signal, in
 * RetroArch's slang (the retrotv mod's default preset, openswos-tv.slangp). OpenSWOS's own, GPL 3.
 *
 * CURVATURE: a barrel -- each point pushed out by the square of its distance along the other
 * axis; outside the bent picture, black.
 * NTSC: the picture encoded as a television carries it -- Y plus I and Q on a subcarrier a cycle
 * each two source pixels, half a cycle further each line and each frame -- and decoded with box
 * filters of whole cycles (Y one, I two, Q three): a flat colour comes back as it was, edges and
 * fine detail bleed, fringe and crawl. Our own model, no filter's code.
 */
#pragma parameter CURVATURE "Curvature" 0.4 0.0 1.0 0.05
#pragma parameter NTSC "Composite signal (NTSC)" 1.0 0.0 1.0 0.05

layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 OutputSize;
    uint FrameCount;
    float CURVATURE;
    float NTSC;
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

const float PI = 3.14159265;

vec3 rgb2yiq(vec3 c)
{
    return vec3(dot(c, vec3(0.299, 0.587, 0.114)),
                dot(c, vec3(0.596, -0.274, -0.322)),
                dot(c, vec3(0.211, -0.523, 0.312)));
}

vec3 yiq2rgb(vec3 y)
{
    return vec3(y.x + 0.956 * y.y + 0.621 * y.z,
                y.x - 0.272 * y.y - 0.647 * y.z,
                y.x - 1.106 * y.y + 1.703 * y.z);
}

/* The composite picture at t. The subcarrier runs a cycle each two source pixels and the taps
 * are a quarter of a pixel apart, so 8 taps are one cycle; the ends of each box at half weight. */
vec3 ntsc(vec2 t)
{
    float line = floor(t.y * params.SourceSize.y);
    float phase0 = PI * (line + float(params.FrameCount & 1u));
    float u0 = t.x * params.SourceSize.x;
    float ys = 0.0, is = 0.0, qs = 0.0;
    for (int k = -12; k <= 12; k++) {
        float d = float(k) * 0.25;
        float ph = PI * (u0 + d) + phase0;
        vec3 s3 = rgb2yiq(texture(Source, vec2(t.x + d * params.SourceSize.z, t.y)).rgb);
        float s = s3.x + s3.y * cos(ph) + s3.z * sin(ph);
        int a = abs(k);
        float wy = a < 4 ? 1.0 : (a == 4 ? 0.5 : 0.0);
        float wi = a < 8 ? 1.0 : (a == 8 ? 0.5 : 0.0);
        float wq = a < 12 ? 1.0 : 0.5;
        ys += s * wy;
        is += 2.0 * s * cos(ph) * wi;
        qs += 2.0 * s * sin(ph) * wq;
    }
    return clamp(yiq2rgb(vec3(ys / 8.0, is / 16.0, qs / 24.0)), 0.0, 1.0);
}

void main()
{
    vec2 t = vTexCoord;
    if (params.CURVATURE > 0.0) {
        vec2 p = t * 2.0 - 1.0;
        p *= 1.0 + params.CURVATURE * 0.12 * (p.yx * p.yx);
        t = p * 0.5 + 0.5;
        if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    vec3 col = texture(Source, t).rgb;
    if (params.NTSC > 0.0)
        col = mix(col, ntsc(t), params.NTSC);
    FragColor = vec4(col, 1.0);
}
