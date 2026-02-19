#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float fragLifeRatio;
layout(location = 3) in float fragHeightAboveEmitter;
layout(location = 4) in vec3 fragWorldPos;

layout(set = 2, binding = 0) uniform ParticleParams {
    vec3 emitterPosition;
    float time;
    vec3 baseColor;
    float particleLifetime;
    vec3 tipColor;
    float maxParticles;
    vec3 gravity;
    float spawnRadius;
    vec3 initialVelocity;
    float particleScale;
    float fadeInDuration;
    float fadeOutDuration;
    int billboardMode;
    int colorMode;
    float velocityRandomness;
    float scaleOverLifetime;
    float rotationSpeed;
    float padding1;
    vec3 windDirection;
    float windStrength;
    float windInfluence;
    float padding2;
    float padding3;
    float padding4;
} params;

layout(location = 0) out vec4 outColor;

void main() {
    const float SKYBOX_RADIUS = 300.0;
    float distFromCenter = length(fragWorldPos);
    
    if (distFromCenter > SKYBOX_RADIUS) {
        discard;
    }
    
    vec2 coord = fragTexCoord * 2.0 - 1.0;
    float dist = length(coord);
    
    if (dist > 1.0) {
        discard;
    }
    
    float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
    
    float fadeIn = min(1.0, fragLifeRatio / params.fadeInDuration);
    float fadeOut = min(1.0, (1.0 - fragLifeRatio) / params.fadeOutDuration);
    alpha *= fadeIn * fadeOut;
    
    vec3 finalColor = fragColor;
    
    if (params.colorMode == 0) {
        float heightFactor = clamp(fragHeightAboveEmitter / 5.0, 0.0, 1.0);
        
        vec3 fireColor = mix(params.baseColor, vec3(1.0, 0.4, 0.0), fragLifeRatio * 0.5);
        
        vec3 smokeColor = mix(vec3(0.3, 0.3, 0.3), params.tipColor, heightFactor);
        
        float fireToSmokeTransition = smoothstep(0.3, 0.7, heightFactor);
        finalColor = mix(fireColor, smokeColor, fireToSmokeTransition);
        
        finalColor = mix(finalColor, params.tipColor, fragLifeRatio * 0.3);
        
        if (heightFactor > 0.5) {
            alpha *= (1.0 - (heightFactor - 0.5) * 0.3);
        }
    }
    
    outColor = vec4(finalColor, alpha);
}