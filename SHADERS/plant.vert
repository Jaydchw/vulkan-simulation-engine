#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec3 fragLighting;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec3 eyePos;
    float time;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 windDirection;
    float windStrength;
    float time;
    float swayAmount;
    float swaySpeed;
    float isPlant;
} push;

vec3 applyWindSway(vec3 position, vec3 worldPos) {
    if (push.isPlant < 0.5) {
        return position;
    }
    
    float height = max(0.0, position.y);
    float normalizedHeight = clamp(height / 5.0, 0.0, 1.0);
    float heightFactor = normalizedHeight * normalizedHeight * normalizedHeight;
    
    float uniquePhase = fract(sin(dot(worldPos.xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318;
    float phase = push.time * push.swaySpeed + uniquePhase;
    
    float primaryWave = sin(phase) * 0.7;
    float secondaryWave = sin(phase * 2.3 + 1.7) * 0.2;
    float tertiaryWave = sin(phase * 4.1 + 3.2) * 0.1;
    float combinedWave = primaryWave + secondaryWave + tertiaryWave;
    
    float windFactor = clamp(push.windStrength / 10.0, 0.0, 1.0);
    
    vec3 normalizedWindDir = normalize(push.windDirection);
    vec3 swayOffset = normalizedWindDir * combinedWave * heightFactor * push.swayAmount * windFactor;
    
    vec3 crossWind = normalize(cross(normalizedWindDir, vec3(0.0, 1.0, 0.0)));
    float crossWave = sin(phase * 1.7 + 2.1) * 0.15;
    swayOffset += crossWind * crossWave * heightFactor * push.swayAmount * windFactor * 0.3;
    
    return position + swayOffset;
}

void main() {
    vec3 worldPosBase = vec3(push.model * vec4(0.0, 0.0, 0.0, 1.0));
    vec3 swayedPosition = applyWindSway(inPosition, worldPosBase);
    
    vec4 worldPos = push.model * vec4(swayedPosition, 1.0);
    
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragPos = worldPos.xyz;
    
    fragLighting = vec3(1.0);
}