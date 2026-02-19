#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in float inParticleIndex;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec3 eyePos;
    float time;
} camera;

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

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out float fragLifeRatio;
layout(location = 3) out float fragHeightAboveEmitter;
layout(location = 4) out vec3 fragWorldPos;

const float PI = 3.14159265359;

uint hash(uint x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

float random(uint seed) {
    return float(hash(seed)) / 4294967295.0;
}

vec3 randomVec3(uint seed) {
    return vec3(
        random(seed) * 2.0 - 1.0,
        random(seed + 1u) * 2.0 - 1.0,
        random(seed + 2u) * 2.0 - 1.0
    );
}

void main() {
    uint particleId = uint(inParticleIndex);
    
    float particleOffset = (inParticleIndex / params.maxParticles) * params.particleLifetime;
    float particleTime = mod(params.time + particleOffset, params.particleLifetime);
    float lifeRatio = particleTime / params.particleLifetime;
    
    uint seedPos = particleId * 3u;
    uint seedVel = particleId * 3u + 100u;
    
    float spawnAngle = random(seedPos) * 2.0 * PI;
    float spawnDist = random(seedPos + 1u) * params.spawnRadius;
    vec3 spawnOffset = vec3(
        cos(spawnAngle) * spawnDist,
        (random(seedPos + 2u) - 0.5) * params.spawnRadius * 0.5,
        sin(spawnAngle) * spawnDist
    );
    
    vec3 randomVel = randomVec3(seedVel) * params.velocityRandomness;
    vec3 velocity = params.initialVelocity + randomVel;
    
    vec3 windForce = params.windDirection * params.windStrength * params.windInfluence;
    
    vec3 particlePos = params.emitterPosition + spawnOffset;
    particlePos += velocity * particleTime;
    particlePos += params.gravity * particleTime * particleTime * 0.5;
    particlePos += windForce * particleTime * particleTime * 0.5;
    
    float scaleModifier = mix(1.0, params.scaleOverLifetime, lifeRatio);
    float scale = params.particleScale * scaleModifier;
    
    vec3 toCamera = normalize(camera.eyePos - particlePos);
    vec3 right, up;
    
    if (params.billboardMode == 0) {
        vec3 worldUp = vec3(0.0, 1.0, 0.0);
        
        if (abs(dot(toCamera, worldUp)) > 0.99) {
            worldUp = vec3(0.0, 0.0, 1.0);
        }
        
        right = normalize(cross(worldUp, toCamera));
        up = normalize(cross(toCamera, right));
    } else if (params.billboardMode == 1) {
        up = vec3(0.0, 1.0, 0.0);
        right = normalize(cross(up, toCamera));
        toCamera = normalize(cross(right, up));
    } else {
        right = vec3(1.0, 0.0, 0.0);
        up = vec3(0.0, 1.0, 0.0);
    }
    
    float rotation = params.rotationSpeed * particleTime + random(particleId + 200u) * 2.0 * PI;
    float cosRot = cos(rotation);
    float sinRot = sin(rotation);
    
    vec2 quadPos = vec2(inPosition.x, inPosition.y);
    vec2 rotatedPos = vec2(
        quadPos.x * cosRot - quadPos.y * sinRot,
        quadPos.x * sinRot + quadPos.y * cosRot
    );
    
    vec3 billboardPos = particlePos + (right * rotatedPos.x + up * rotatedPos.y) * scale;
    
    gl_Position = camera.proj * camera.view * vec4(billboardPos, 1.0);
    
    if (params.colorMode == 0) {
        fragColor = mix(params.baseColor, params.tipColor, lifeRatio);
    } else if (params.colorMode == 1) {
        fragColor = params.baseColor;
    } else {
        fragColor = params.tipColor;
    }
    
    fragTexCoord = inTexCoord;
    fragLifeRatio = lifeRatio;
    fragHeightAboveEmitter = particlePos.y - params.emitterPosition.y;
    fragWorldPos = particlePos;
}