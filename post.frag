#version 430 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D uHdr;
uniform vec2  uTexel;      // 1 / size of mip level 0
uniform float uExposure;
uniform float uBloom;
uniform float uVignette;
uniform float uTime;

// 9-tap tent filter on a mip level: smooth, blocky-free bloom from the mip chain
vec3 tent(float lod) {
    vec2 t = uTexel * exp2(lod);
    vec3 s = textureLod(uHdr, TexCoord, lod).rgb * 4.0;
    s += (textureLod(uHdr, TexCoord + vec2( t.x, 0.0), lod).rgb
        + textureLod(uHdr, TexCoord + vec2(-t.x, 0.0), lod).rgb
        + textureLod(uHdr, TexCoord + vec2(0.0,  t.y), lod).rgb
        + textureLod(uHdr, TexCoord + vec2(0.0, -t.y), lod).rgb) * 2.0;
    s += textureLod(uHdr, TexCoord + t, lod).rgb
       + textureLod(uHdr, TexCoord - t, lod).rgb
       + textureLod(uHdr, TexCoord + vec2(t.x, -t.y), lod).rgb
       + textureLod(uHdr, TexCoord + vec2(-t.x, t.y), lod).rgb;
    return s / 16.0;
}

// ACES filmic (Stephen Hill fit)
vec3 aces(vec3 c) {
    const mat3 inM = mat3(0.59719, 0.07600, 0.02840,
                          0.35458, 0.90834, 0.13383,
                          0.04823, 0.01566, 0.83777);
    const mat3 outM = mat3( 1.60475, -0.10208, -0.00327,
                           -0.53108,  1.10813, -0.07276,
                           -0.07367, -0.00605,  1.07602);
    c = inM * c;
    vec3 a = c * (c + 0.0245786) - 0.000090537;
    vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
    return clamp(outM * (a / b), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec3 hdr = textureLod(uHdr, TexCoord, 0.0).rgb;
    vec3 bloom = tent(1.0) * 0.30 + tent(2.0) * 0.25 + tent(3.0) * 0.20
               + tent(4.0) * 0.12 + tent(5.0) * 0.08 + tent(6.0) * 0.05;
    vec3 c = mix(hdr, bloom, uBloom) * uExposure;
    c = aces(c);

    vec2 q = TexCoord - 0.5;
    c *= 1.0 - uVignette * dot(q, q) * 1.6;

    c = linearToSrgb(c);
    // tiny dither to kill banding in the dark glow
    float n = fract(sin(dot(gl_FragCoord.xy + uTime, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    FragColor = vec4(c, 1.0);
}
