#version 450
layout(set = 1, binding = 0) uniform samplerCube skyboxSampler;
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 domeCenter;
    float domeRadiusSquared;
    float timeOfDay;
    float sunIntensity;
    vec2 padding;
} push;
layout(location = 0) in vec3 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragViewPos;
layout(location = 0) out vec4 outColor;

vec3 adjustForTimeOfDay(vec3 color, float sunIntensity) {
    float brightness = max(0.1, sunIntensity);
    
    float saturation = mix(0.2, 1.0, smoothstep(0.0, 1.0, sunIntensity));
    
    vec3 grayscale = vec3(dot(color, vec3(0.299, 0.587, 0.114)));
    vec3 desaturated = mix(grayscale, color, saturation);
    
    vec3 finalColor = desaturated * brightness;
    
    if (sunIntensity < 0.35) {
        vec3 nightTint = vec3(0.2, 0.25, 0.4);
        float nightAmount = smoothstep(0.35, 0.0, sunIntensity);
        finalColor = mix(finalColor, finalColor * nightTint, nightAmount);
    }
    
    return finalColor;
}

void main() {
    vec3 toDome = camera.eyePos - push.domeCenter;
    float distSquared = dot(toDome, toDome);
    
    if (distSquared >= push.domeRadiusSquared) {
        discard;
    }
    
    vec3 viewDir = normalize(fragWorldPos - camera.eyePos);
    
    vec3 texCoord = viewDir;
    texCoord.x = -texCoord.x;
    
    vec3 skyboxColor = texture(skyboxSampler, texCoord).rgb;
    
    vec3 adjustedColor = adjustForTimeOfDay(skyboxColor, push.sunIntensity);
    
    outColor = vec4(adjustedColor, 1.0);
}