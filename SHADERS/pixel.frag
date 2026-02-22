#version 450

layout(binding = 0) uniform sampler2D screenTexture;

layout(push_constant) uniform PushConstants {
    float hue;
    float saturation;
    float contrast;
    float chromaticAberration;
    float vignetteStrength;
    float sharpenStrength;
    float exposure;
    float gamma;
    float filmGrain;
    float temperature;
    float pixelResolution;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// --- UTILS ---

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 adjustHue(vec3 color, float hueShift) {
    vec3 hsv = rgb2hsv(color);
    hsv.x = fract(hsv.x + hueShift);
    return hsv2rgb(hsv);
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

vec3 adjustTemperature(vec3 color, float temp) {
    color.r += temp * 0.1;
    color.b -= temp * 0.1;
    return clamp(color, 0.0, 1.0);
}

vec3 tonemap_aces(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vignette(vec2 uv) {
    uv *= 1.0 - uv.yx;
    float vig = uv.x * uv.y * 15.0;
    return pow(vig, 0.6);
}

// Palette of 32 hand-picked pixel art colors (extended AAP-like palette)
const vec3 PALETTE[32] = vec3[32](
    // Darks
    vec3(0.05, 0.05, 0.09),  // near-black
    vec3(0.16, 0.11, 0.20),  // dark purple
    vec3(0.26, 0.15, 0.18),  // dark maroon
    vec3(0.12, 0.17, 0.26),  // dark navy
    // Mid-darks
    vec3(0.36, 0.20, 0.22),  // brown-red
    vec3(0.22, 0.28, 0.39),  // steel blue
    vec3(0.30, 0.35, 0.22),  // olive
    vec3(0.45, 0.30, 0.18),  // brown
    // Mids
    vec3(0.60, 0.35, 0.22),  // warm brown
    vec3(0.55, 0.42, 0.55),  // mauve
    vec3(0.35, 0.50, 0.45),  // teal
    vec3(0.50, 0.50, 0.60),  // blue-grey
    vec3(0.65, 0.50, 0.30),  // tan
    vec3(0.40, 0.55, 0.30),  // grass green
    vec3(0.30, 0.45, 0.65),  // medium blue
    vec3(0.70, 0.38, 0.38),  // salmon
    // Mid-lights
    vec3(0.80, 0.55, 0.30),  // orange
    vec3(0.90, 0.65, 0.35),  // gold
    vec3(0.55, 0.75, 0.45),  // light green
    vec3(0.45, 0.65, 0.80),  // sky blue
    vec3(0.80, 0.60, 0.60),  // pink
    vec3(0.70, 0.75, 0.55),  // lime
    vec3(0.55, 0.55, 0.80),  // periwinkle
    vec3(0.85, 0.45, 0.45),  // red
    // Lights
    vec3(0.95, 0.80, 0.50),  // light gold
    vec3(0.75, 0.90, 0.70),  // pale green
    vec3(0.70, 0.85, 0.95),  // pale blue
    vec3(0.95, 0.75, 0.70),  // peach
    vec3(0.90, 0.90, 0.80),  // cream
    vec3(0.85, 0.80, 0.95),  // lavender
    vec3(0.95, 0.95, 0.90),  // near-white
    vec3(1.00, 1.00, 1.00)   // white
);

vec3 nearestPaletteColor(vec3 color) {
    float bestDist = 1e10;
    vec3 bestColor = PALETTE[0];
    for (int i = 0; i < 32; i++) {
        vec3 diff = color - PALETTE[i];
        float d = dot(diff, diff);
        if (d < bestDist) {
            bestDist = d;
            bestColor = PALETTE[i];
        }
    }
    return bestColor;
}

void main() {
    vec2 texSize = vec2(textureSize(screenTexture, 0));
    float pixelSize = pc.pixelResolution;

    // Snap UV to pixel grid
    vec2 pixelUV = floor(fragTexCoord * texSize / pixelSize) * pixelSize / texSize;

    // Sample the center of each "big pixel" with a slight area average
    // for smoother downscale (2x2 tap average within the pixel cell)
    vec2 halfPixel = (pixelSize * 0.25) / texSize;
    vec3 s0 = texture(screenTexture, pixelUV + vec2(-halfPixel.x, -halfPixel.y)).rgb;
    vec3 s1 = texture(screenTexture, pixelUV + vec2( halfPixel.x, -halfPixel.y)).rgb;
    vec3 s2 = texture(screenTexture, pixelUV + vec2(-halfPixel.x,  halfPixel.y)).rgb;
    vec3 s3 = texture(screenTexture, pixelUV + vec2( halfPixel.x,  halfPixel.y)).rgb;
    vec3 color = (s0 + s1 + s2 + s3) * 0.25;

    // Chromatic aberration (subtle, applied before pixelization look)
    float ca = pc.chromaticAberration;
    if (ca > 0.0) {
        vec2 caDir = pixelUV - vec2(0.5);
        float rr = texture(screenTexture, pixelUV - caDir * ca).r;
        float bb = texture(screenTexture, pixelUV + caDir * ca).b;
        color.r = mix(color.r, rr, 0.5);
        color.b = mix(color.b, bb, 0.5);
    }

    // Exposure + tone mapping
    color *= pc.exposure;
    color = tonemap_aces(color);
    color = pow(color, vec3(1.0 / pc.gamma));

    // Color grading
    color = adjustHue(color, pc.hue);
    color = adjustSaturation(color, pc.saturation);
    color = adjustContrast(color, pc.contrast);
    color = adjustTemperature(color, pc.temperature);

    // Quantize to limited color palette for authentic pixel art look
    // Boost saturation slightly before palette matching for more vivid result
    vec3 hsv = rgb2hsv(color);
    hsv.y = min(1.0, hsv.y * 1.3);
    color = hsv2rgb(hsv);
    color = nearestPaletteColor(color);

    // Subtle edge darkening per pixel block for definition
    vec2 cellUV = fract(fragTexCoord * texSize / pixelSize);
    float edgeDist = min(min(cellUV.x, 1.0 - cellUV.x), min(cellUV.y, 1.0 - cellUV.y));
    float edgeLine = smoothstep(0.0, 0.08, edgeDist);
    color *= mix(0.88, 1.0, edgeLine);

    // Film grain
    if (pc.filmGrain > 0.0) {
        float noise = hash(fragTexCoord * texSize + fract(pc.filmGrain * 12.9898)) * 2.0 - 1.0;
        color += vec3(noise) * pc.filmGrain;
        color = clamp(color, 0.0, 1.0);
    }

    // Vignette
    float vig = vignette(fragTexCoord);
    color *= mix(1.0 - pc.vignetteStrength, 1.0, vig);

    outColor = vec4(color, 1.0);
}
