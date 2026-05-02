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

const float VIGNETTE_EXTENT = 0.6;


vec3 tonemap_aces(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

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

vec3 adjustTemperature(vec3 color, float temp) {
    color.r += temp * 0.1;
    color.b -= temp * 0.1;
    return clamp(color, 0.0, 1.0);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vignette(vec2 uv) {
    uv *= 1.0 - uv.yx;
    float vig = uv.x * uv.y * 15.0;
    return pow(vig, VIGNETTE_EXTENT);
}

vec3 sharpen(sampler2D tex, vec2 uv, float strength) {
    vec2 texelSize = 1.0 / textureSize(tex, 0);
    
    vec3 center = texture(tex, uv).rgb;
    vec3 top    = texture(tex, uv + vec2(0.0, texelSize.y)).rgb;
    vec3 bottom = texture(tex, uv - vec2(0.0, texelSize.y)).rgb;
    vec3 left   = texture(tex, uv - vec2(texelSize.x, 0.0)).rgb;
    vec3 right  = texture(tex, uv + vec2(texelSize.x, 0.0)).rgb;
    
    vec3 edge = -top - bottom - left - right + center * 5.0;
    return center + edge * strength;
}

void main() {
    vec3 color = sharpen(screenTexture, fragTexCoord, pc.sharpenStrength);

    float ca = pc.chromaticAberration;
    if (ca > 0.0) {
        vec2 caDir = fragTexCoord - vec2(0.5);
        float r = texture(screenTexture, fragTexCoord - caDir * ca).r;
        float b = texture(screenTexture, fragTexCoord + caDir * ca).b;
        color.r = mix(color.r, r, 0.5);
        color.b = mix(color.b, b, 0.5);
    }

    color *= pc.exposure;

    color = tonemap_aces(color);

    color = pow(color, vec3(1.0 / pc.gamma));

    color = adjustHue(color, pc.hue);
    color = adjustSaturation(color, pc.saturation);
    color = adjustContrast(color, pc.contrast);

    color = adjustTemperature(color, pc.temperature);

    if (pc.filmGrain > 0.0) {
        vec2 texSize = vec2(textureSize(screenTexture, 0));
        float noise = hash(fragTexCoord * texSize + fract(pc.filmGrain * 12.9898)) * 2.0 - 1.0;
        color += vec3(noise) * pc.filmGrain;
        color = clamp(color, 0.0, 1.0);
    }

    float vig = vignette(fragTexCoord);
    color *= mix(1.0 - pc.vignetteStrength, 1.0, vig);

    outColor = vec4(color, 1.0);
}