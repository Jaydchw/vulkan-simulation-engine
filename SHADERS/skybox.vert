#version 450
layout(location = 0) in vec3 inPosition;
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec3 domeCenter;
    float domeRadiusSquared;
} push;

layout(location = 0) out vec3 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragViewPos;

void main() {
    fragTexCoord = normalize(inPosition);
    
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragViewPos = camera.eyePos;
    
    vec4 pos = camera.proj * camera.view * worldPos;
    gl_Position = pos.xyww;
}