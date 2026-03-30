#pragma once
#include <string>

using PhysicsMaterialID = uint32_t;
constexpr PhysicsMaterialID INVALID_PHYSICS_MATERIAL_ID = 0;

struct PhysicsMaterial {
    std::string name;
    float density = 1000.0f;
};

struct PhysicsMaterialInteraction {
    float restitution     = 0.5f;
    float staticFriction  = 0.4f;
    float dynamicFriction = 0.3f;
};
