#version 450

layout(binding = 0) uniform sampler2D screenTexture;

// Added Weather Inputs
layout(push_constant) uniform PushConstants {
    float temperature;
    float humidity;
} pushConstants;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// --- CONFIGURATION ---
const float GAMMA = 1.0;  // Kept your preferred 1.0
const float EXPOSURE = 1.0;
const float BASE_SATURATION = 0.9; // Renamed to separate from weather
const float BASE_CONTRAST = 1.0;
const float VIGNETTE_STRENGTH = 0.3;
const float VIGNETTE_EXTENT = 0.6;
const float CHROMATIC_ABERRATION = 0.003; // Slight bump so you can see it

// Weather Ranges
const float TEMP_MIN = -10.0;
const float TEMP_MAX = 40.0;
const float HUMIDITY_MIN = 0.0;
const float HUMIDITY_MAX = 1.0;

// --- UTILS ---

vec3 tonemap_aces(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 adjustSaturation(vec3 color, float saturation) {
    // Rec.709 Luma for better accuracy than 0.299/0.587
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

vec3 adjustContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

float vignette(vec2 uv) {
    uv *= 1.0 - uv.yx;
    float vig = uv.x * uv.y * 15.0;
    return pow(vig, VIGNETTE_EXTENT);
}

// Calculates a subtle color filter based on weather
vec3 getWeatherTint(float temp, float humidity) {
    float tNorm = clamp((temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0.0, 1.0);
    
    // 1. Temperature Tint (Hue Shift)
    // Cold: Subtle Icy Blue (0.9, 0.95, 1.05)
    // Hot: Subtle Warm Amber (1.05, 1.0, 0.92)
    vec3 coldTint = vec3(0.92, 0.96, 1.05); 
    vec3 hotTint  = vec3(1.05, 1.02, 0.94);
    
    // Mix based on temperature
    vec3 tempTint = mix(coldTint, hotTint, tNorm);

    // 2. Humidity Tint
    // High humidity: Very subtle Green/Teal shift (thick air)
    vec3 dryTint = vec3(1.0, 1.0, 1.0);
    vec3 humidTint = vec3(0.98, 1.0, 0.98); // Tiny green shift
    vec3 humidityColor = mix(dryTint, humidTint, humidity);

    // Combine them
    return tempTint * humidityColor;
}

vec3 sharpen(sampler2D tex, vec2 uv) {
    vec2 texelSize = 1.0 / textureSize(tex, 0);
    
    vec3 center = texture(tex, uv).rgb;
    vec3 top    = texture(tex, uv + vec2(0.0, texelSize.y)).rgb;
    vec3 bottom = texture(tex, uv - vec2(0.0, texelSize.y)).rgb;
    vec3 left   = texture(tex, uv - vec2(texelSize.x, 0.0)).rgb;
    vec3 right  = texture(tex, uv + vec2(texelSize.x, 0.0)).rgb;
    
    // Standard kernel
    vec3 edge = -top - bottom - left - right + center * 5.0;
    return center + edge * 0.3; // 0.3 is sharpen strength
}

void main() {
    // 1. Base Sampling (Sharpen is the 'base' look)
    vec3 color = sharpen(screenTexture, fragTexCoord);

    // 2. Apply Chromatic Aberration manually on top 
    // (Simple R/B offset based on sharpened center G)
    vec2 caDir = fragTexCoord - vec2(0.5);
    float r = texture(screenTexture, fragTexCoord - caDir * CHROMATIC_ABERRATION).r;
    float b = texture(screenTexture, fragTexCoord + caDir * CHROMATIC_ABERRATION).b;
    // We mix the CA Red/Blue with the sharpened Green for a composite look
    // Or just apply CA tinting to the sharpened result subtly. 
    // For simplicity/cleanness, let's just stick to the sharpen output 
    // but allowing the edges to bleed slightly if you really want CA:
    if (CHROMATIC_ABERRATION > 0.0) {
        color.r = mix(color.r, r, 0.5);
        color.b = mix(color.b, b, 0.5);
    }

    // 3. APPLY WEATHER TINT (The requested feature)
    // This happens in Linear Space before Tone Mapping
    vec3 weatherTint = getWeatherTint(pushConstants.temperature, pushConstants.humidity);
    color *= weatherTint;

    // 4. Exposure
    color *= EXPOSURE;
    
    // 5. Tone Mapping
    color = tonemap_aces(color);
    
    // 6. Gamma
    color = pow(color, vec3(1.0 / GAMMA));
    
    // 7. Grading
    color = adjustSaturation(color, BASE_SATURATION);
    color = adjustContrast(color, BASE_CONTRAST);
    
    // 8. Vignette
    float vig = vignette(fragTexCoord);
    color *= mix(1.0 - VIGNETTE_STRENGTH, 1.0, vig);
    
    outColor = vec4(color, 1.0);
}