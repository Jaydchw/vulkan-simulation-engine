#version 450

layout(binding = 0) uniform sampler2D screenTexture;

// Keep PushConstants compatible with your pipeline layout
layout(push_constant) uniform PushConstants {
    float temperature;
    float humidity;
} pushConstants;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// --- TOON SETTINGS ---
const float EDGE_THRESHOLD = 0.2; // Lower = more sensitive edges
const float COLOR_LEVELS = 5.0;   // Number of color bands
const float EDGE_STRENGTH = 1.0;  // 1.0 = black edges

// Standard Sobel Kernels for edge detection
const mat3 sx = mat3( 
    1.0, 2.0, 1.0, 
    0.0, 0.0, 0.0, 
   -1.0, -2.0, -1.0 
);
const mat3 sy = mat3( 
    1.0, 0.0, -1.0, 
    2.0, 0.0, -2.0, 
    1.0, 0.0, -1.0 
);

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

void main() {
    vec2 texSize = textureSize(screenTexture, 0);
    
    // 1. Edge Detection (Sobel)
    mat3 I;
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            vec3 sampleColor = texture(screenTexture, fragTexCoord + vec2(i-1, j-1) / texSize).rgb;
            // Use luminance for edge detection
            I[i][j] = length(sampleColor); 
        }
    }

    float gX = dot(sx[0], I[0]) + dot(sx[1], I[1]) + dot(sx[2], I[2]); 
    float gY = dot(sy[0], I[0]) + dot(sy[1], I[1]) + dot(sy[2], I[2]);
    float g = sqrt(gX*gX + gY*gY);

    // 2. Color Quantization (Posterization)
    vec3 color = texture(screenTexture, fragTexCoord).rgb;
    
    // Optional: Keep your weather tint so it matches the world state
    // (You can copy the getWeatherTint function from your original shader if desired)
    
    // Quantize only the Value (brightness) channel for better results
    vec3 hsv = rgb2hsv(color);
    hsv.z = floor(hsv.z * COLOR_LEVELS) / COLOR_LEVELS;
    // Slightly boost saturation for "cartoony" look
    hsv.y = min(1.0, hsv.y * 1.2); 
    color = hsv2rgb(hsv);

    // 3. Mix Edge
    float edge = step(EDGE_THRESHOLD, g);
    vec3 finalColor = mix(color, vec3(0.0), edge * EDGE_STRENGTH);

    outColor = vec4(finalColor, 1.0);
}