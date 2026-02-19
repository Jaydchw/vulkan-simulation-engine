#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(constant_id = 0) const int SHADING_MODE = 0;

layout(binding = 0, set = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec3 eyePos;
} ubo;

struct LightData {
    mat4 lightSpaceMatrix;
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
    float cutOff;
    float outerCutOff;
    int type;
    int castsShadows;
    int shadowMapIndex;
    float padding1;
    float padding2;
    float padding3;
};

layout(binding = 1, set = 0) uniform LightBuffer {
    LightData lights[8];
    int numLights;
    int numShadowMaps;
    float padding1;
    float padding2;
} lightBuffer;

layout(binding = 0, set = 1) uniform MaterialProperties {
    vec4 albedoColor;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;
    float indexOfRefraction;
    float heightScale;
    float textureScale;
    float padding2;
} material;

layout(binding = 1, set = 1) uniform sampler2D albedoMap;
layout(binding = 2, set = 1) uniform sampler2D normalMap;
layout(binding = 3, set = 1) uniform sampler2D roughnessMap;
layout(binding = 4, set = 1) uniform sampler2D metallicMap;
layout(binding = 5, set = 1) uniform sampler2D emissiveMap;
layout(binding = 6, set = 1) uniform sampler2D heightMap;
layout(binding = 7, set = 1) uniform sampler2D aoMap;

layout(binding = 0, set = 2) uniform sampler2D shadowMaps[4];

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint layerMask;
    uint cameraLayer;
} pushConstants;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragLighting;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float sampleShadowMap(int index, vec3 shadowCoord) {
    float closestDepth;
    switch (index) {
        case 0: closestDepth = texture(shadowMaps[0], shadowCoord.xy).r; break;
        case 1: closestDepth = texture(shadowMaps[1], shadowCoord.xy).r; break;
        case 2: closestDepth = texture(shadowMaps[2], shadowCoord.xy).r; break;
        case 3: closestDepth = texture(shadowMaps[3], shadowCoord.xy).r; break;
        default: return 1.0;
    }
    return shadowCoord.z > closestDepth + 0.0005 ? 0.0 : 1.0;
}

vec2 getShadowMapTexelSize(int index) {
    switch (index) {
        case 0: return 1.0 / textureSize(shadowMaps[0], 0);
        case 1: return 1.0 / textureSize(shadowMaps[1], 0);
        case 2: return 1.0 / textureSize(shadowMaps[2], 0);
        case 3: return 1.0 / textureSize(shadowMaps[3], 0);
        default: return vec2(1.0 / 4096.0);
    }
}

float calculateShadow(int lightIndex, vec3 fragPos, vec3 normal, vec3 lightDir) {
    if (lightBuffer.lights[lightIndex].castsShadows == 0) {
        return 1.0;
    }
    
    int shadowMapIndex = lightBuffer.lights[lightIndex].shadowMapIndex;
    if (shadowMapIndex < 0 || shadowMapIndex >= 4) {
        return 1.0;
    }
    
    vec4 fragPosLightSpace = lightBuffer.lights[lightIndex].lightSpaceMatrix * vec4(fragPos, 1.0);
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }
    
    if (projCoords.z > 1.0 || projCoords.z < 0.0) {
        return 1.0;
    }
    
    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(0.05 * (1.0 - cosTheta), 0.005);
    
    float shadow = 0.0;
    vec2 texelSize = getShadowMapTexelSize(shadowMapIndex);
    
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            vec3 shadowCoord = vec3(projCoords.xy + offset, projCoords.z - bias);
            shadow += sampleShadowMap(shadowMapIndex, shadowCoord);
        }
    }
    shadow /= 9.0;
    
    return shadow;
}

vec3 calculatePointLight(LightData light, int lightIndex, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 albedo, float roughness, float metallic) {
    vec3 lightDir = normalize(light.position.xyz - fragPos);
    float distance = length(light.position.xyz - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
    
    float diff = max(dot(normal, lightDir), 0.0);
    
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), (1.0 - roughness) * 128.0);
    
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(halfwayDir, viewDir), 0.0), 5.0);
    
    vec3 kS = fresnel;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    
    vec3 diffuse = kD * albedo / PI;
    vec3 specular = kS * spec;
    
    float shadow = calculateShadow(lightIndex, fragPos, normal, lightDir);
    
    return (diffuse + specular) * light.color.xyz * light.intensity * diff * attenuation * shadow;
}

vec3 calculateSunLight(LightData light, int lightIndex, vec3 normal, vec3 viewDir, vec3 albedo, float roughness, float metallic) {
    vec3 lightDir = normalize(-light.direction.xyz);
    
    float diff = max(dot(normal, lightDir), 0.0);
    
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), (1.0 - roughness) * 128.0);
    
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(halfwayDir, viewDir), 0.0), 5.0);
    
    vec3 kS = fresnel;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    
    vec3 diffuse = kD * albedo / PI;
    vec3 specular = kS * spec;

    //float shadow = 1.0;
    float shadow = calculateShadow(lightIndex, fragWorldPos, normal, lightDir);
    
    return (diffuse + specular) * light.color.xyz * light.intensity * diff * shadow;
}

void main() {
    if ((pushConstants.layerMask & pushConstants.cameraLayer) == 0) {
        discard;
    }

    vec2 scaledTexCoord = fragTexCoord * material.textureScale;
    
    vec4 albedoSample = texture(albedoMap, scaledTexCoord) * material.albedoColor;
    vec3 albedo = albedoSample.rgb;
    float alpha = albedoSample.a * material.opacity;
    
    // DEBUG: Uncomment to test if light data is reaching the shader
    // outColor = vec4(float(lightBuffer.numLights) / 8.0, 0.0, 0.0, 1.0); return;
    // outColor = vec4(lightBuffer.lights[0].color.rgb, 1.0); return;
    // outColor = vec4(lightBuffer.lights[0].intensity / 10.0, 0.0, 0.0, 1.0); return;
    
    if (SHADING_MODE == 0) {
        vec3 normal = normalize(fragNormal);
        vec3 viewDir = normalize(ubo.eyePos - fragWorldPos);
        
        float roughness = material.roughness;
        float metallic = material.metallic;
        
        vec3 ambient = vec3(0.03) * albedo;
        vec3 lighting = ambient;
        
        for (int i = 0; i < lightBuffer.numLights && i < 8; i++) {
            LightData light = lightBuffer.lights[i];
            
            if (light.type == 0) {
                lighting += calculatePointLight(light, i, normal, fragWorldPos, viewDir, albedo, roughness, metallic);
            } else if (light.type == 1) {
                lighting += calculateSunLight(light, i, normal, viewDir, albedo, roughness, metallic);
            }
        }
        
        outColor = vec4(lighting, alpha);
    } else {
        outColor = vec4(fragLighting * albedo, alpha);
    }
}