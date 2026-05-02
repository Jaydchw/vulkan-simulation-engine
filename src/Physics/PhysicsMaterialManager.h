#pragma once
#include <unordered_map>
#include <vector>

#include "Physics/PhysicsMaterial.h"

class PhysicsMaterialManager {
public:
    PhysicsMaterialID registerMaterial(const PhysicsMaterial& mat) {
        const PhysicsMaterialID id = static_cast<PhysicsMaterialID>(materials.size() + 1);
        nameToID[mat.name] = id;
        materials.push_back(mat);
        return id;
    }

    void registerInteraction(PhysicsMaterialID a, PhysicsMaterialID b,
                             const PhysicsMaterialInteraction& interaction) {
        interactions[interactionKey(a, b)] = interaction;
    }

    const PhysicsMaterial* getMaterial(PhysicsMaterialID id) const {
        if (id == INVALID_PHYSICS_MATERIAL_ID || id > materials.size()) return nullptr;
        return &materials[id - 1];
    }

    PhysicsMaterialID findIDByName(const std::string& name) const {
        auto it = nameToID.find(name);
        return it != nameToID.end() ? it->second : INVALID_PHYSICS_MATERIAL_ID;
    }

    PhysicsMaterialInteraction findInteraction(PhysicsMaterialID a, PhysicsMaterialID b) const {
        auto it = interactions.find(interactionKey(a, b));
        if (it != interactions.end()) return it->second;
        it = interactions.find(interactionKey(a, a));
        if (it != interactions.end()) return it->second;
        it = interactions.find(interactionKey(b, b));
        if (it != interactions.end()) return it->second;
        return {};
    }

    void clear() {
        materials.clear();
        nameToID.clear();
        interactions.clear();
    }

    const std::vector<PhysicsMaterial>& allMaterials() const { return materials; }

private:
    static uint64_t interactionKey(PhysicsMaterialID a, PhysicsMaterialID b) {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }

    std::vector<PhysicsMaterial> materials;
    std::unordered_map<std::string, PhysicsMaterialID> nameToID;
    std::unordered_map<uint64_t, PhysicsMaterialInteraction> interactions;
};
