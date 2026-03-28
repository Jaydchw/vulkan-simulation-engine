#include "FBSceneLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Scene_generated.h"
#include "ECS/Components.h"
#include "ECS/EntityBuilder.h"
#include "Util/Debug.h"

namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool bval = false; double nval = 0.0; std::string sval;
    std::vector<Json> aval;
    std::vector<std::pair<std::string,Json>> oval;

    static const Json& nil() { static Json n; return n; }
    bool isNull()   const { return type == Type::Null;   }
    bool isArray()  const { return type == Type::Array;  }
    bool isObject() const { return type == Type::Object; }
    size_t size()   const { return aval.size();          }

    bool has(const std::string& k) const {
        if (type != Type::Object) return false;
        for (auto& p : oval) if (p.first == k) return true;
        return false;
    }
    const Json& operator[](const std::string& k) const {
        if (type != Type::Object) return nil();
        for (auto& p : oval) if (p.first == k) return p.second;
        return nil();
    }
    const Json& operator[](size_t i) const { return i < aval.size() ? aval[i] : nil(); }

    float       asFloat(float   d = 0.0f)          const { return type==Type::Number ? float(nval) : d; }
    int         asInt  (int     d = 0)              const { return type==Type::Number ? int(nval)   : d; }
    bool        asBool (bool    d = false)          const { return type==Type::Bool   ? bval         : d; }
    std::string asStr  (const std::string& d = {})  const { return type==Type::String ? sval         : d; }
};

class JsonParser {
    const char* p; const char* end;
    void skip() {
        while (p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p;
    }
    Json parseString() {
        Json j; j.type = Json::Type::String; ++p;
        while (p<end && *p!='"') {
            if (*p=='\\') { ++p; if (p<end) { switch(*p){case '"':j.sval+='"';break;case '\\':j.sval+='\\';break;case 'n':j.sval+='\n';break;case 't':j.sval+='\t';break;default:j.sval+=*p;} } }
            else j.sval += *p;
            ++p;
        }
        if (p<end) ++p; return j;
    }
    Json parseNumber() {
        Json j; j.type = Json::Type::Number; const char* s=p;
        if (p<end && *p=='-') ++p;
        while (p<end && *p>='0' && *p<='9') ++p;
        if (p<end && *p=='.') { ++p; while (p<end && *p>='0'&&*p<='9') ++p; }
        if (p<end && (*p=='e'||*p=='E')) { ++p; if (p<end&&(*p=='+'||*p=='-'))++p; while(p<end&&*p>='0'&&*p<='9')++p; }
        j.nval = std::stod(std::string(s, p)); return j;
    }
    Json parseValue() {
        skip();
        if (p>=end) return {};
        if (*p=='"') return parseString();
        if (*p=='{') {
            Json j; j.type=Json::Type::Object; ++p;
            while (true) { skip(); if (p>=end||*p=='}') break; if (*p==','){++p;continue;} auto k=parseString(); skip(); if(p<end&&*p==':')++p; j.oval.push_back({k.sval,parseValue()}); }
            if (p<end)++p; return j;
        }
        if (*p=='[') {
            Json j; j.type=Json::Type::Array; ++p;
            while (true) { skip(); if (p>=end||*p==']') break; if (*p==','){++p;continue;} j.aval.push_back(parseValue()); }
            if (p<end)++p; return j;
        }
        if (*p=='t') { p+=4; Json j; j.type=Json::Type::Bool; j.bval=true; return j; }
        if (*p=='f') { p+=5; Json j; j.type=Json::Type::Bool; j.bval=false; return j; }
        if (*p=='n') { p+=4; return {}; }
        return parseNumber();
    }
public:
    JsonParser(const std::string& text) : p(text.c_str()), end(text.c_str()+text.size()) {}
    Json parse() { return parseValue(); }
};

glm::vec3 jVec3(const Json& j, glm::vec3 d={}) {
    if (j.isNull()) return d;
    return { j["x"].asFloat(d.x), j["y"].asFloat(d.y), j["z"].asFloat(d.z) };
}

struct JTransform { glm::vec3 position; glm::vec3 eulerDeg; glm::vec3 scale={1,1,1}; };
JTransform jTransform(const Json& j) {
    JTransform t;
    if (j.isNull()) return t;
    t.position = jVec3(j["position"]);
    const Json& o = j["orientation"];
    if (!o.isNull()) t.eulerDeg = { o["pitch"].asFloat(), o["yaw"].asFloat(), o["roll"].asFloat() };
    t.scale = jVec3(j["scale"], {1,1,1});
    return t;
}

} // namespace

namespace {

constexpr float PI = 3.14159265358979f;
float sphereVol  (float r)          { return (4.f/3.f)*PI*r*r*r; }
float cuboidVol  (glm::vec3 s)      { return s.x*s.y*s.z; }
float cylinderVol(float r, float h) { return PI*r*r*h; }
float capsuleVol (float r, float h) { return PI*r*r*h + (4.f/3.f)*PI*r*r*r; }

glm::quat eulerToQuat(glm::vec3 degPYR) {
    return glm::quat(glm::radians(degPYR));
}

static int anonCount = 0;

} // namespace

FBSceneLoader::FBSceneLoader(MeshManager* mm, MaterialManager* matm)
    : meshManager(mm), materialManager(matm) {}

std::vector<std::string> FBSceneLoader::listScenes(const std::string& directory) {
    std::vector<std::string> results;
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) return results;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext == ".bin" || ext == ".fbscene")
            results.push_back(entry.path().string());
    }
    std::sort(results.begin(), results.end());
    return results;
}

std::string FBSceneLoader::interactionKey(const std::string& a, const std::string& b) {
    return (a < b) ? (a + ":" + b) : (b + ":" + a);
}

FBSceneLoader::Interaction FBSceneLoader::findInteraction(const std::string& a,
                                                          const std::string& b) const {
    auto it = interactions.find(interactionKey(a, b));
    if (it != interactions.end()) return it->second;
    it = interactions.find(interactionKey(a, a));
    if (it != interactions.end()) return it->second;
    return {};
}

void FBSceneLoader::buildObject(Registry& registry,
                                const std::string& name,
                                glm::vec3 position,
                                glm::vec3 eulerDeg,
                                glm::vec3 scale,
                                uint8_t   shapeType,
                                float     sphereRadius,
                                glm::vec3 cuboidSize,
                                float     capsRadius,
                                float     capsHeight,
                                float     cylRadius,
                                float     cylHeight,
                                glm::vec3 planeNormal,
                                uint8_t   behaviourType,
                                glm::vec3 linearVel,
                                glm::vec3 angularVelDeg,
                                bool      gravityOn,
                                const std::string& materialName) const
{
    MaterialID matID   = materialManager->getDefaultMaterial();
    float      density = 1000.0f;
    {
        auto mi = namedMaterials.find(materialName);
        if (mi != namedMaterials.end()) matID = mi->second;
        auto di = materialDensities.find(materialName);
        if (di != materialDensities.end()) density = di->second;
    }

    Interaction inter = findInteraction(materialName, materialName);
    MeshID meshID = meshManager->getDefaultCube();
    EntityBuilder builder;

    switch (shapeType) {
        case 1: {
            meshID = meshManager->createSphere(sphereRadius);
            builder.sphereCollider(sphereRadius);
            if (behaviourType == 2) builder.mass(density * sphereVol(sphereRadius));
            break;
        }
        case 2: {
            meshID = meshManager->createPlane(200.0f, 200.0f);
            builder.planeCollider(glm::normalize(planeNormal));
            break;
        }
        case 3: {
            meshID = meshManager->createCapsule(capsRadius, capsHeight);
            builder.capsuleCollider(capsRadius, capsHeight);
            if (behaviourType == 2) builder.mass(density * capsuleVol(capsRadius, capsHeight));
            break;
        }
        case 4: {
            meshID = meshManager->createCylinder(cylRadius, cylHeight);
            builder.cylinderCollider(cylRadius, cylHeight);
            if (behaviourType == 2) builder.mass(density * cylinderVol(cylRadius, cylHeight));
            break;
        }
        case 5: {
            meshID = meshManager->createCube(1.0f);
            scale *= cuboidSize;
            builder.boxCollider(cuboidSize * 0.5f);
            if (behaviourType == 2) builder.mass(density * cuboidVol(cuboidSize));
            break;
        }
        default: {
            meshID = meshManager->createSphere(0.5f);
            builder.sphereCollider(0.5f);
            break;
        }
    }

    builder.name(name)
           .position(position)
           .rotation(eulerToQuat(eulerDeg))
           .scale(scale)
           .mesh(meshID)
           .material(matID)
           .restitution(inter.restitution);

    if (behaviourType == 2) {
        builder.velocity(linearVel)
               .angularVelocity(glm::radians(angularVelDeg))
               .useGravity(gravityOn)
               .damping(0.99f);
    }

    builder.build(registry);
}

void FBSceneLoader::buildSpawner(Registry& registry,
                                 const std::string& name,
                                 float     startTime,
                                 float     spawnInterval,
                                 int       maxSpawns,
                                 glm::vec3 spawnPos,
                                 float     posRandomness,
                                 glm::vec3 avgLinVel,
                                 bool      gravityOn,
                                 const std::string& materialName,
                                 uint8_t   spawnerShape,
                                 float     rMin, float rMax,
                                 float     hMin, float hMax,
                                 glm::vec3 sMin, glm::vec3 sMax) const
{
    MaterialID matID   = materialManager->getDefaultMaterial();
    float      density = 1000.0f;
    {
        auto mi = namedMaterials.find(materialName);
        if (mi != namedMaterials.end()) matID = mi->second;
        auto di = materialDensities.find(materialName);
        if (di != materialDensities.end()) density = di->second;
    }

    Interaction inter = findInteraction(materialName, materialName);

    float rAvg = (rMin + rMax) * 0.5f;
    float hAvg = (hMin + hMax) * 0.5f;
    glm::vec3 sAvg = (sMin + sMax) * 0.5f;

    SpawnTemplate tmpl;
    tmpl.weight              = 1.0f;
    tmpl.materialID          = matID;
    tmpl.namePrefix          = name;
    tmpl.physics.useGravity  = gravityOn;
    tmpl.physics.damping     = 0.99f;
    tmpl.physics.restitution = inter.restitution;

    switch (spawnerShape) {
        case 1: {
            tmpl.meshID          = meshManager->createSphere(rAvg);
            tmpl.collider.type   = ColliderType::Sphere;
            tmpl.collider.radius = rAvg;
            tmpl.physics.mass    = density * sphereVol(rAvg);
            break;
        }
        case 2: {
            tmpl.meshID            = meshManager->createCylinder(rAvg, hAvg);
            tmpl.collider.type     = ColliderType::Cylinder;
            tmpl.collider.radius   = rAvg;
            tmpl.collider.height   = hAvg;
            tmpl.physics.mass      = density * cylinderVol(rAvg, hAvg);
            break;
        }
        case 3: {
            tmpl.meshID            = meshManager->createCapsule(rAvg, hAvg);
            tmpl.collider.type     = ColliderType::Capsule;
            tmpl.collider.radius   = rAvg;
            tmpl.collider.height   = hAvg;
            tmpl.physics.mass      = density * capsuleVol(rAvg, hAvg);
            break;
        }
        case 4: {
            tmpl.meshID               = meshManager->createCube(1.0f);
            tmpl.scale                = sAvg;
            tmpl.collider.type        = ColliderType::AABB;
            tmpl.collider.halfExtents = sAvg * 0.5f;
            tmpl.physics.mass         = density * cuboidVol(sAvg);
            break;
        }
        default: {
            tmpl.meshID          = meshManager->createSphere(0.3f);
            tmpl.collider.type   = ColliderType::Sphere;
            tmpl.collider.radius = 0.3f;
            tmpl.physics.mass    = density * sphereVol(0.3f);
            break;
        }
    }

    float speed = glm::length(avgLinVel);
    glm::vec3 dir = (speed > 1e-4f) ? glm::normalize(avgLinVel) : glm::vec3(0,1,0);

    SpawnerComponent sc;
    sc.templates          = { tmpl };
    sc.spawnInterval      = spawnInterval;
    sc.spawnDirection     = dir;
    sc.spawnSpeed         = speed;
    sc.positionRandomness = posRandomness;
    sc.maxSpawns          = maxSpawns;
    sc.timer              = -startTime;
    sc.enabled            = true;

    Entity e = registry.createEntity();
    registry.addComponent<NameComponent>(e, { name });
    TransformComponent tc;
    tc.position = spawnPos;
    tc.rotation = glm::quat(1,0,0,0);
    tc.scale    = glm::vec3(1);
    registry.addComponent<TransformComponent>(e, tc);
    registry.addComponent<SpawnerComponent>(e, sc);
}

bool FBSceneLoader::loadBinary(const std::string& filepath, Registry& registry,
                               FBWorldSettings& settings) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Debug::log(Debug::Category::MAIN, "FBSceneLoader: Cannot open binary: ", filepath);
        return false;
    }
    auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> buf(size);
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));

    flatbuffers::Verifier verifier(buf.data(), buf.size());
    if (!Simulation::VerifySceneBuffer(verifier)) {
        Debug::log(Debug::Category::MAIN, "FBSceneLoader: Invalid or corrupt binary: ", filepath);
        return false;
    }

    const auto* scene = Simulation::GetScene(buf.data());

    settings.name        = scene->name()        ? scene->name()->str()        : "";
    settings.description = scene->description() ? scene->description()->str() : "";
    settings.gravityOn   = scene->gravity_on();

    static const glm::vec3 defaultColors[] = {
        {0.8f,0.8f,0.8f},{0.6f,0.3f,0.1f},{0.2f,0.5f,0.8f},
        {0.9f,0.7f,0.2f},{0.3f,0.8f,0.3f},{0.8f,0.2f,0.2f}
    };
    int colorIdx = 0;

    if (scene->materials()) {
        for (const auto* mat : *scene->materials()) {
            if (!mat || !mat->name()) continue;
            std::string n = mat->name()->str();
            materialDensities[n] = mat->density();
            glm::vec3 col = defaultColors[colorIdx++ % 6];
            MaterialBuilder b;
            b.name(n).albedoColor(col).roughness(0.5f).metallic(0.0f);
            namedMaterials[n] = materialManager->registerMaterial(b);
        }
    }

    if (scene->interactions()) {
        for (const auto* i : *scene->interactions()) {
            if (!i || !i->material_a() || !i->material_b()) continue;
            std::string a = i->material_a()->str();
            std::string b = i->material_b()->str();
            Interaction intr;
            intr.restitution     = i->restitution();
            intr.staticFriction  = i->static_friction();
            intr.dynamicFriction = i->dynamic_friction();
            interactions[interactionKey(a, b)] = intr;
        }
    }

    if (scene->cameras()) {
        for (const auto* cam : *scene->cameras()) {
            if (!cam) continue;
            std::string name = cam->name() ? cam->name()->str() : "Camera";

            glm::vec3 pos = {}, euler = {}, scale = {1,1,1};
            if (const auto* t = cam->transform()) {
                pos   = { t->position().x(),    t->position().y(),    t->position().z() };
                euler = { t->orientation().yaw(), t->orientation().pitch(), t->orientation().roll() };
                scale = { t->scale().x(),        t->scale().y(),        t->scale().z() };
            }

            CameraComponent camComp;
            if (cam->camera_type_type() == Simulation::CameraType::OrthographicCamera) {
                const auto* oc = cam->camera_type_as_OrthographicCamera();
                camComp.type             = CameraType::Orthographic;
                camComp.orthographicSize = oc ? oc->size() : 50.0f;
                camComp.nearPlane        = oc ? oc->near() : 0.1f;
                camComp.farPlane         = oc ? oc->far()  : 1000.0f;
            } else {
                const auto* pc = cam->camera_type_as_PerspectiveCamera();
                camComp.type      = CameraType::Perspective;
                camComp.fov       = pc ? pc->fov()  : 60.0f;
                camComp.nearPlane = pc ? pc->near() : 0.1f;
                camComp.farPlane  = pc ? pc->far()  : 1000.0f;
            }

            Entity e = registry.createEntity();
            registry.addComponent<NameComponent>(e, { name });
            TransformComponent tc;
            tc.position = pos;
            tc.rotation = eulerToQuat(euler);
            tc.scale    = scale;
            registry.addComponent<TransformComponent>(e, tc);
            registry.addComponent<CameraComponent>(e, camComp);
        }
    }

    if (scene->objects()) {
        for (const auto* obj : *scene->objects()) {
            if (!obj) continue;
            std::string name = obj->name()     ? obj->name()->str()     : "object " + std::to_string(++anonCount);
            std::string mat  = obj->material() ? obj->material()->str() : "";

            glm::vec3 pos = {}, euler = {}, scale = {1,1,1};
            if (const auto* t = obj->transform()) {
                pos   = { t->position().x(),     t->position().y(),     t->position().z() };
                euler = { t->orientation().yaw(), t->orientation().pitch(), t->orientation().roll() };
                scale = { t->scale().x(),         t->scale().y(),         t->scale().z() };
            }

            uint8_t shapeType = static_cast<uint8_t>(obj->shape_type());
            uint8_t behavType = static_cast<uint8_t>(obj->behaviour_type());

            float     sphereR  = 0.5f;
            glm::vec3 cuboidSz = {1,1,1};
            glm::vec3 planeN   = {0,1,0};
            float     capsR    = 0.5f, capsH = 1.0f;

            switch (obj->shape_type()) {
                case Simulation::Shape::Sphere:
                    if (const auto* s = obj->shape_as_Sphere()) sphereR = s->radius();
                    break;
                case Simulation::Shape::Plane:
                    if (const auto* s = obj->shape_as_Plane(); s && s->normal()) planeN = { s->normal()->x(), s->normal()->y(), s->normal()->z() };
                    break;
                case Simulation::Shape::Capsule:
                    if (const auto* s = obj->shape_as_Capsule()) { capsR = s->radius(); capsH = s->height(); }
                    break;
                case Simulation::Shape::Cylinder:
                    if (const auto* s = obj->shape_as_Cylinder()) { capsR = s->radius(); capsH = s->height(); }
                    break;
                case Simulation::Shape::Cuboid:
                    if (const auto* s = obj->shape_as_Cuboid(); s && s->size()) cuboidSz = { s->size()->x(), s->size()->y(), s->size()->z() };
                    break;
                default: break;
            }

            glm::vec3 linVel = {}, angVelDeg = {};
            if (obj->behaviour_type() == Simulation::Behaviour::SimulatedObject) {
                if (const auto* sim = obj->behaviour_as_SimulatedObject()) {
                    if (const auto* ps = sim->initial_state()) {
                        linVel    = { ps->linear_velocity().x(),  ps->linear_velocity().y(),  ps->linear_velocity().z() };
                        angVelDeg = { ps->angular_velocity().x(), ps->angular_velocity().y(), ps->angular_velocity().z() };
                    }
                }
            }

            buildObject(registry, name, pos, euler, scale,
                        shapeType, sphereR, cuboidSz, capsR, capsH, capsR, capsH, planeN,
                        behavType, linVel, angVelDeg, settings.gravityOn, mat);
        }
    }

    const auto* typesVec  = scene->spawners_type();
    const auto* valuesVec = scene->spawners();
    if (typesVec && valuesVec) {
        auto count = std::min(typesVec->size(), valuesVec->size());
        for (flatbuffers::uoffset_t i = 0; i < count; ++i) {
            auto         stype = typesVec->Get(i);
            const void*  sdata = valuesVec->Get(i);
            if (!sdata) continue;

            uint8_t                         spEnum = static_cast<uint8_t>(stype);
            const Simulation::BaseSpawner*  base   = nullptr;
            float     rMin = 0.25f, rMax = 0.5f, hMin = 0.5f, hMax = 1.0f;
            glm::vec3 sMin = {0.5f,0.5f,0.5f}, sMax = {1,1,1};

            switch (stype) {
                case Simulation::SpawnerType::SphereSpawner: {
                    const auto* ss = static_cast<const Simulation::SphereSpawner*>(sdata);
                    base = ss->base();
                    if (const auto* r = ss->radius_range()) { rMin = r->min(); rMax = r->max(); }
                    break;
                }
                case Simulation::SpawnerType::CylinderSpawner: {
                    const auto* cs = static_cast<const Simulation::CylinderSpawner*>(sdata);
                    base = cs->base();
                    if (const auto* r = cs->radius_range()) { rMin = r->min(); rMax = r->max(); }
                    if (const auto* h = cs->height_range()) { hMin = h->min(); hMax = h->max(); }
                    break;
                }
                case Simulation::SpawnerType::CapsuleSpawner: {
                    const auto* cs = static_cast<const Simulation::CapsuleSpawner*>(sdata);
                    base = cs->base();
                    if (const auto* r = cs->radius_range()) { rMin = r->min(); rMax = r->max(); }
                    if (const auto* h = cs->height_range()) { hMin = h->min(); hMax = h->max(); }
                    break;
                }
                case Simulation::SpawnerType::CuboidSpawner: {
                    const auto* cs = static_cast<const Simulation::CuboidSpawner*>(sdata);
                    base = cs->base();
                    if (const auto* sr = cs->size_range()) {
                        sMin = { sr->min().x(), sr->min().y(), sr->min().z() };
                        sMax = { sr->max().x(), sr->max().y(), sr->max().z() };
                    }
                    break;
                }
                default: continue;
            }
            if (!base) continue;

            std::string spName    = base->name() ? base->name()->str() : "Spawner";
            float       startTime = base->start_time();
            float       interval  = 1.0f;
            int         maxSp     = -1;

            switch (base->spawn_type_type()) {
                case Simulation::SpawnType::SingleBurstSpawn:
                    if (const auto* sb = base->spawn_type_as_SingleBurstSpawn()) {
                        interval = 0.01f;
                        maxSp    = static_cast<int>(sb->count());
                    }
                    break;
                case Simulation::SpawnType::RepeatingSpawn:
                    if (const auto* rp = base->spawn_type_as_RepeatingSpawn()) {
                        interval = rp->interval();
                        if (rp->max_count() > 0) maxSp = static_cast<int>(rp->max_count());
                    }
                    break;
                default: break;
            }

            glm::vec3 spawnPos  = {};
            float     posRandom = 0.0f;

            switch (base->location_type()) {
                case Simulation::SpawnLocation::FixedLocation:
                    if (const auto* fl = base->location_as_FixedLocation()) {
                        if (const auto* t = fl->transform())
                            spawnPos = { t->position().x(), t->position().y(), t->position().z() };
                    }
                    break;
                case Simulation::SpawnLocation::RandomBox:
                    if (const auto* rb = base->location_as_RandomBox(); rb && rb->min() && rb->max()) {
                        glm::vec3 mn = { rb->min()->x(), rb->min()->y(), rb->min()->z() };
                        glm::vec3 mx = { rb->max()->x(), rb->max()->y(), rb->max()->z() };
                        spawnPos  = (mn + mx) * 0.5f;
                        posRandom = glm::length((mx - mn) * 0.5f);
                    }
                    break;
                case Simulation::SpawnLocation::RandomSphere:
                    if (const auto* rs = base->location_as_RandomSphere(); rs && rs->center()) {
                        spawnPos  = { rs->center()->x(), rs->center()->y(), rs->center()->z() };
                        posRandom = rs->radius();
                    }
                    break;
                default: break;
            }

            glm::vec3 avgLin = {};
            if (const auto* lv = base->linear_velocity()) {
                avgLin = {
                    (lv->min().x() + lv->max().x()) * 0.5f,
                    (lv->min().y() + lv->max().y()) * 0.5f,
                    (lv->min().z() + lv->max().z()) * 0.5f
                };
            }

            std::string spMat = base->material() ? base->material()->str() : "";

            buildSpawner(registry, spName, startTime, interval, maxSp, spawnPos, posRandom,
                         avgLin, settings.gravityOn, spMat, spEnum, rMin, rMax, hMin, hMax, sMin, sMax);
        }
    }

    Debug::log(Debug::Category::MAIN, "FBSceneLoader: Loaded binary scene '", settings.name, "'");
    return true;
}

bool FBSceneLoader::loadJSON(const std::string& filepath, Registry& registry,
                             FBWorldSettings& settings) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Debug::log(Debug::Category::MAIN, "FBSceneLoader: Cannot open: ", filepath);
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    Json root;
    try { root = JsonParser(text).parse(); } catch (...) {
        Debug::log(Debug::Category::MAIN, "FBSceneLoader: JSON parse error: ", filepath);
        return false;
    }
    if (!root.isObject()) return false;

    settings.name        = root["name"].asStr("Unnamed");
    settings.description = root["description"].asStr();
    settings.gravityOn   = root["gravity_on"].asBool(true);

    static const glm::vec3 defColors[] = {
        {0.8f,0.8f,0.8f},{0.6f,0.3f,0.1f},{0.2f,0.5f,0.8f},
        {0.9f,0.7f,0.2f},{0.3f,0.8f,0.3f},{0.8f,0.2f,0.2f}
    };
    int colorIdx = 0;
    const Json& mats = root["materials"];
    if (mats.isArray()) {
        for (size_t i = 0; i < mats.size(); ++i) {
            std::string n = mats[i]["name"].asStr();
            if (n.empty()) continue;
            float density = mats[i]["density"].asFloat(1000.0f);
            materialDensities[n] = density;
            glm::vec3 col = defColors[colorIdx++ % 6];
            MaterialBuilder b; b.name(n).albedoColor(col).roughness(0.5f).metallic(0.0f);
            namedMaterials[n] = materialManager->registerMaterial(b);
        }
    }

    const Json& intr = root["interactions"];
    if (intr.isArray()) {
        for (size_t i = 0; i < intr.size(); ++i) {
            std::string a = intr[i]["material_a"].asStr();
            std::string b = intr[i]["material_b"].asStr();
            if (a.empty()||b.empty()) continue;
            Interaction it;
            it.restitution     = intr[i]["restitution"].asFloat(0.5f);
            it.staticFriction  = intr[i]["static_friction"].asFloat(0.4f);
            it.dynamicFriction = intr[i]["dynamic_friction"].asFloat(0.3f);
            interactions[interactionKey(a,b)] = it;
        }
    }

    const Json& cams = root["cameras"];
    if (cams.isArray()) {
        for (size_t i = 0; i < cams.size(); ++i) {
            const Json& c = cams[i];
            std::string nm = c["name"].asStr("Camera");
            auto t = jTransform(c["transform"]);
            CameraComponent cam;
            const Json& ct = c["camera_type"];
            if (!ct.isNull()) {
                if (ct["type"].asStr() == "OrthographicCamera") {
                    cam.type = CameraType::Orthographic;
                    cam.orthographicSize = ct["size"].asFloat(50.0f);
                    cam.nearPlane = ct["near"].asFloat(0.1f);
                    cam.farPlane  = ct["far"].asFloat(1000.0f);
                } else {
                    cam.type = CameraType::Perspective;
                    cam.fov       = ct["fov"].asFloat(60.0f);
                    cam.nearPlane = ct["near"].asFloat(0.1f);
                    cam.farPlane  = ct["far"].asFloat(1000.0f);
                }
            }
            Entity e = registry.createEntity();
            registry.addComponent<NameComponent>(e, {nm});
            TransformComponent tc; tc.position = t.position; tc.rotation = eulerToQuat(t.eulerDeg); tc.scale = t.scale;
            registry.addComponent<TransformComponent>(e, tc);
            registry.addComponent<CameraComponent>(e, cam);
        }
    }

    const Json& objs = root["objects"];
    if (objs.isArray()) {
        for (size_t i = 0; i < objs.size(); ++i) {
            const Json& obj = objs[i];
            std::string nm  = obj["name"].asStr();
            if (nm.empty()) nm = "object " + std::to_string(++anonCount);
            auto        t   = jTransform(obj["transform"]);
            std::string mat = obj["material"].asStr();
            const Json& sh  = obj["shape"];
            const Json& bh  = obj["behaviour"];
            std::string shType = sh["type"].asStr();
            std::string bhType = bh["type"].asStr("StaticObject");

            uint8_t shEnum = 0, bhEnum = 1;
            if      (shType=="Sphere")   shEnum=1;
            else if (shType=="Plane")    shEnum=2;
            else if (shType=="Capsule")  shEnum=3;
            else if (shType=="Cylinder") shEnum=4;
            else if (shType=="Cuboid")   shEnum=5;
            if      (bhType=="SimulatedObject") bhEnum=2;
            else if (bhType=="AnimatedObject")  bhEnum=3;

            float     sRadius  = sh["radius"].asFloat(0.5f);
            glm::vec3 cuboidSz = jVec3(sh["size"], {1,1,1});
            float     cRadius  = sh["radius"].asFloat(0.5f);
            float     cHeight  = sh["height"].asFloat(1.0f);
            glm::vec3 planeN   = jVec3(sh["normal"], {0,1,0});
            glm::vec3 linVel   = jVec3(bh["linear_velocity"]);
            glm::vec3 angVelD  = jVec3(bh["angular_velocity"]);

            buildObject(registry, nm, t.position, t.eulerDeg, t.scale,
                        shEnum, sRadius, cuboidSz, cRadius, cHeight, cRadius, cHeight, planeN,
                        bhEnum, linVel, angVelD, settings.gravityOn, mat);
        }
    }

    const Json& sps = root["spawners"];
    if (sps.isArray()) {
        for (size_t i = 0; i < sps.size(); ++i) {
            const Json& sp   = sps[i];
            const Json& base = sp["base"];
            if (base.isNull()) continue;
            std::string spType = sp["type"].asStr();
            uint8_t spEnum = 0;
            if      (spType=="SphereSpawner")   spEnum=1;
            else if (spType=="CylinderSpawner") spEnum=2;
            else if (spType=="CapsuleSpawner")  spEnum=3;
            else if (spType=="CuboidSpawner")   spEnum=4;

            std::string nm  = base["name"].asStr("Spawner");
            float startTime = base["start_time"].asFloat(0.0f);
            float interval  = 1.0f; int maxSp = -1;
            const Json& st  = base["spawn_type"];
            if (!st.isNull()) {
                if (st["type"].asStr()=="RepeatingSpawn") { interval=st["interval"].asFloat(1.0f); int mc=st["max_count"].asInt(0); if(mc>0)maxSp=mc; }
                else if (st["type"].asStr()=="SingleBurstSpawn") { interval=0.01f; maxSp=st["count"].asInt(1); }
            }
            glm::vec3 pos={}; float posR=0.0f;
            const Json& loc = base["location"];
            if (!loc.isNull()) {
                std::string lt=loc["type"].asStr();
                if      (lt=="FixedLocation")  pos=jTransform(loc["transform"]).position;
                else if (lt=="RandomBox")      { glm::vec3 mn=jVec3(loc["min"]),mx=jVec3(loc["max"]); pos=(mn+mx)*0.5f; posR=glm::length((mx-mn)*0.5f); }
                else if (lt=="RandomSphere")   { pos=jVec3(loc["center"]); posR=loc["radius"].asFloat(1.0f); }
            }
            glm::vec3 linMin=jVec3(base["linear_velocity"]["min"]), linMax=jVec3(base["linear_velocity"]["max"]);
            glm::vec3 avgLin=(linMin+linMax)*0.5f;
            std::string spMat = base["material"].asStr();

            float rMin=0.25f,rMax=0.5f,hMin=0.5f,hMax=1.0f;
            glm::vec3 sMin={0.5f,0.5f,0.5f},sMax={1,1,1};
            if      (spEnum==1) { rMin=sp["radius_range"]["min"].asFloat(0.25f); rMax=sp["radius_range"]["max"].asFloat(0.5f); }
            else if (spEnum==2||spEnum==3) { rMin=sp["radius_range"]["min"].asFloat(0.25f); rMax=sp["radius_range"]["max"].asFloat(0.5f); hMin=sp["height_range"]["min"].asFloat(0.5f); hMax=sp["height_range"]["max"].asFloat(1.0f); }
            else if (spEnum==4) { sMin=jVec3(sp["size_range"]["min"],{0.5f,0.5f,0.5f}); sMax=jVec3(sp["size_range"]["max"],{1,1,1}); }

            buildSpawner(registry, nm, startTime, interval, maxSp, pos, posR, avgLin, settings.gravityOn, spMat, spEnum, rMin, rMax, hMin, hMax, sMin, sMax);
        }
    }

    Debug::log(Debug::Category::MAIN, "FBSceneLoader: Loaded JSON scene '", settings.name, "'");
    return true;
}

bool FBSceneLoader::load(const std::string& filepath, Registry& registry,
                         FBWorldSettings& settings) {
    namedMaterials.clear();
    materialDensities.clear();
    interactions.clear();
    settings = FBWorldSettings{};

    auto ext = std::filesystem::path(filepath).extension().string();
    if (ext == ".bin") return loadBinary(filepath, registry, settings);
    return loadJSON(filepath, registry, settings);
}
