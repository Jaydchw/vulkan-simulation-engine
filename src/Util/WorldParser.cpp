#include "WorldParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ECS/EntityBuilder.h"
#include "Util/Debug.h"

WorldParser::WorldParser(MeshManager* mm, RenderMaterialManager* matm,
                         TextureManager* tm)
    : meshManager(mm), materialManager(matm), textureManager(tm) {}

std::vector<std::string> WorldParser::listWorlds(
    const std::string& directory) {
  std::vector<std::string> results;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) return results;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".world") {
      results.push_back(entry.path().string());
    }
  }
  std::sort(results.begin(), results.end());
  return results;
}

bool WorldParser::load(const std::string& filepath, Registry& registry,
                       WorldSettings& settings) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    Debug::log(Debug::Category::MAIN, "WorldParser: Failed to open: ",
               filepath);
    return false;
  }

  Debug::log(Debug::Category::MAIN, "WorldParser: Loading world: ", filepath);

  namedTextures.clear();
  namedMaterials.clear();
  namedMeshes.clear();

  settings = WorldSettings{};

  std::string line;
  int lineNum = 0;

  while (std::getline(file, line)) {
    lineNum++;
    line = trim(line);

    if (line.empty() || line[0] == '#') continue;

    if (line == "BeginSettings") {
      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndSettings") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "ClearColor")
          settings.clearColor = parseVec4(val);
        else if (key == "TimeSpeed")
          settings.timeSpeed = parseFloat(val);
        else if (key == "SimulationHz")
          settings.simulationHz = parseInt(val);
        else if (key == "MaxFps")
          settings.maxFps = parseInt(val);
        else if (key == "KillboxEnabled")
          settings.killboxEnabled = parseBool(val);
        else if (key == "KillboxY")
          settings.killboxY = parseFloat(val);
        else if (key == "Wind")
          settings.environment.wind = parseVec3(val);
        else if (key == "WindDrag")
          settings.environment.windDrag = parseFloat(val);
        else if (key == "WindAffects") {
          if (val == "AllObjects")
            settings.environment.windAffects = WindAffectsMode::AllObjects;
          else
            settings.environment.windAffects = WindAffectsMode::ClothOnly;
        } else if (key == "WindAffectsAllObjects") {
          settings.environment.windAffects = parseBool(val)
              ? WindAffectsMode::AllObjects
              : WindAffectsMode::ClothOnly;
        }
      }
      continue;
    }

    if (line == "BeginTexture") {
      std::string texName;
      std::string texType = "solid";
      glm::vec3 colorA(1.0f);
      glm::vec3 colorB(0.0f);
      uint32_t size = 256;
      uint32_t divisions = 8;
      uint32_t stripeCount = 8;
      bool vertical = false;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndTexture") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          texName = val;
        else if (key == "Type")
          texType = val;
        else if (key == "Color" || key == "ColorA")
          colorA = parseVec3(val);
        else if (key == "ColorB")
          colorB = parseVec3(val);
        else if (key == "Size")
          size = static_cast<uint32_t>(parseInt(val));
        else if (key == "Divisions")
          divisions = static_cast<uint32_t>(parseInt(val));
        else if (key == "StripeCount")
          stripeCount = static_cast<uint32_t>(parseInt(val));
        else if (key == "Vertical")
          vertical = parseBool(val);
      }

      if (texName.empty()) continue;

      TextureID texID = INVALID_TEXTURE_ID;
      if (texType == "solid")
        texID = ProceduralTexture::solid(textureManager, colorA,
                                         std::max(size, 4u));
      else if (texType == "checker")
        texID = ProceduralTexture::checker(textureManager, colorA, colorB, size,
                                           divisions);
      else if (texType == "gradient")
        texID = ProceduralTexture::linearGradient(textureManager, colorA,
                                                  colorB, size, vertical);
      else if (texType == "radial")
        texID = ProceduralTexture::radialGradient(textureManager, colorA,
                                                  colorB, size);
      else if (texType == "stripe")
        texID = ProceduralTexture::stripe(textureManager, colorA, colorB, size,
                                          stripeCount, vertical);

      if (texID != INVALID_TEXTURE_ID) {
        namedTextures[texName] = texID;
        Debug::log(Debug::Category::MAIN,
                   "WorldParser: Created texture '", texName, "'");
      }
      continue;
    }

    if (line == "BeginMaterial") {
      std::string matName;
      glm::vec3 albedoColor(1.0f);
      float roughness = 0.5f;
      float metallic = 0.0f;
      float textureScale = 1.0f;
      std::string albedoTexture;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndMaterial") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          matName = val;
        else if (key == "AlbedoColor")
          albedoColor = parseVec3(val);
        else if (key == "Roughness")
          roughness = parseFloat(val);
        else if (key == "Metallic")
          metallic = parseFloat(val);
        else if (key == "TextureScale")
          textureScale = parseFloat(val);
        else if (key == "AlbedoTexture")
          albedoTexture = val;
      }

      if (matName.empty()) continue;

      RenderMaterialBuilder builder;
      builder.name(matName);
      builder.albedoColor(albedoColor);
      builder.roughness(roughness);
      builder.metallic(metallic);
      builder.textureScale(textureScale);

      if (!albedoTexture.empty()) {
        auto it = namedTextures.find(albedoTexture);
        if (it != namedTextures.end()) {
          builder.albedoMap(it->second);
        }
      }

      namedMaterials[matName] =
          materialManager->registerMaterial(builder);
      Debug::log(Debug::Category::MAIN,
                 "WorldParser: Created material '", matName, "'");
      continue;
    }

    if (line == "BeginMesh") {
      std::string meshName;
      std::string meshType = "cube";
      float sizeVal = 1.0f;
      float width = 1.0f;
      float height = 1.0f;
      float radius = 1.0f;
      uint32_t segments = 32;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndMesh") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          meshName = val;
        else if (key == "Type")
          meshType = val;
        else if (key == "Size")
          sizeVal = parseFloat(val);
        else if (key == "Width")
          width = parseFloat(val);
        else if (key == "Height")
          height = parseFloat(val);
        else if (key == "Radius")
          radius = parseFloat(val);
        else if (key == "Segments")
          segments = static_cast<uint32_t>(parseInt(val));
      }

      if (meshName.empty()) continue;

      MeshID meshID = INVALID_MESH_ID;
      if (meshType == "cube")
        meshID = meshManager->createCube(sizeVal);
      else if (meshType == "sphere")
        meshID = meshManager->createSphere(radius, segments);
      else if (meshType == "plane")
        meshID = meshManager->createPlane(width, height);
      else if (meshType == "cylinder")
        meshID = meshManager->createCylinder(radius, height, segments);
      else if (meshType == "cone")
        meshID = meshManager->createCone(radius, height, segments);
      else if (meshType == "capsule")
        meshID = meshManager->createCapsule(radius, height, segments);
      else if (meshType == "pyramid")
        meshID = meshManager->createPyramid(sizeVal, height);

      if (meshID != INVALID_MESH_ID) {
        namedMeshes[meshName] = meshID;
        Debug::log(Debug::Category::MAIN,
                   "WorldParser: Created mesh '", meshName, "'");
      }
      continue;
    }

    if (line == "BeginObject") {
      std::string objName = "Unnamed";
      glm::vec3 position(0.0f);
      glm::vec3 eulerRotation(0.0f);
      glm::vec3 objScale(1.0f);
      std::string meshRef;
      std::string materialRef;
      bool visible = true;
      bool hasPhysics = false;
      glm::vec3 velocity(0.0f);
      glm::vec3 angularVelocity(0.0f);
      glm::vec3 constantTorque(0.0f);
      float mass = 1.0f;
      float restitution = 0.5f;
      float physicsDamping = 0.99f;
      bool useGravity = true;
      bool hasCollider = false;
      std::string colliderType;
      float colliderRadius = 1.0f;
      float colliderHeight = 1.0f;
      glm::vec3 colliderHalfExtents(0.5f);
      glm::vec3 colliderNormal(0.0f, 1.0f, 0.0f);
      bool hasAnimation = false;
      AnimationComponent animComp;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndObject") break;

        if (line == "BeginAnimation") {
          hasAnimation = true;
          std::string animPathMode = "stop";
          std::string animEasing   = "linear";

          while (std::getline(file, line)) {
            lineNum++;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line == "EndAnimation") break;

            if (line == "BeginWaypoint") {
              AnimationWaypoint wp;
              while (std::getline(file, line)) {
                lineNum++;
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                if (line == "EndWaypoint") break;
                const size_t weq = line.find('=');
                if (weq == std::string::npos) continue;
                std::string wkey = trim(line.substr(0, weq));
                std::string wval = trim(line.substr(weq + 1));
                if (wkey == "Position")
                  wp.position = parseVec3(wval);
                else if (wkey == "Rotation") {
                  glm::vec3 euler = parseVec3(wval);
                  wp.rotation = glm::quat(glm::radians(euler));
                } else if (wkey == "Time")
                  wp.time = parseFloat(wval);
              }
              animComp.waypoints.push_back(wp);
              continue;
            }

            const size_t aeq = line.find('=');
            if (aeq == std::string::npos) continue;
            std::string akey = trim(line.substr(0, aeq));
            std::string aval = trim(line.substr(aeq + 1));

            if (akey == "PathMode")
              animPathMode = aval;
            else if (akey == "Easing")
              animEasing = aval;
            else if (akey == "TotalDuration")
              animComp.totalDuration = parseFloat(aval);
          }

          if      (animPathMode == "loop")    animComp.pathMode = PathMode::LOOP;
          else if (animPathMode == "reverse") animComp.pathMode = PathMode::REVERSE;
          else                                animComp.pathMode = PathMode::STOP;

          animComp.easing = (animEasing == "smoothstep") ? EasingType::SMOOTHSTEP : EasingType::LINEAR;
          continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          objName = val;
        else if (key == "Position")
          position = parseVec3(val);
        else if (key == "Rotation")
          eulerRotation = parseVec3(val);
        else if (key == "Scale")
          objScale = parseVec3(val);
        else if (key == "Mesh")
          meshRef = val;
        else if (key == "Material")
          materialRef = val;
        else if (key == "Visible")
          visible = parseBool(val);
        else if (key == "Physics") {
          hasPhysics = parseBool(val);
        } else if (key == "Velocity")
          velocity = parseVec3(val);
        else if (key == "Mass") {
          hasPhysics = true;
          mass = parseFloat(val);
        } else if (key == "Restitution") {
          hasPhysics = true;
          restitution = parseFloat(val);
        } else if (key == "Damping") {
          hasPhysics = true;
          physicsDamping = parseFloat(val);
        } else if (key == "UseGravity") {
          hasPhysics = true;
          useGravity = parseBool(val);
        } else if (key == "AngularVelocity") {
          hasPhysics = true;
          angularVelocity = parseVec3(val);
        } else if (key == "ConstantTorque") {
          hasPhysics = true;
          constantTorque = parseVec3(val);
        } else if (key == "Collider") {
          hasCollider = true;
          colliderType = val;
        } else if (key == "ColliderRadius") {
          hasCollider = true;
          colliderRadius = parseFloat(val);
        } else if (key == "ColliderHeight") {
          hasCollider = true;
          colliderHeight = parseFloat(val);
        } else if (key == "ColliderHalfExtents") {
          hasCollider = true;
          colliderHalfExtents = parseVec3(val);
        } else if (key == "ColliderNormal") {
          hasCollider = true;
          colliderNormal = parseVec3(val);
        }
      }

      EntityBuilder builder;
      builder.name(objName);
      builder.position(position);
      if (eulerRotation != glm::vec3(0.0f))
        builder.rotationEuler(eulerRotation);
      builder.scale(objScale);
      builder.visible(visible);

      auto meshIt = namedMeshes.find(meshRef);
      if (meshIt != namedMeshes.end()) builder.mesh(meshIt->second);

      auto matIt = namedMaterials.find(materialRef);
      if (matIt != namedMaterials.end()) builder.renderMaterial(matIt->second);

      if (hasPhysics) {
        builder.mass(mass);
        builder.restitution(restitution);
        builder.damping(physicsDamping);
        builder.useGravity(useGravity);
        if (velocity != glm::vec3(0.0f))
          builder.velocity(velocity);
        if (angularVelocity != glm::vec3(0.0f))
          builder.angularVelocity(angularVelocity);
        if (constantTorque != glm::vec3(0.0f))
          builder.constantTorque(constantTorque);
      }

      if (hasCollider) {
        if (colliderType == "sphere")
          builder.sphereCollider(colliderRadius);
        else if (colliderType == "box")
          builder.boxCollider(colliderHalfExtents);
        else if (colliderType == "plane") {
          if (colliderHalfExtents != glm::vec3(0.5f))
            builder.planeCollider(colliderNormal, colliderHalfExtents);
          else
            builder.planeCollider(colliderNormal);
        } else if (colliderType == "cylinder")
          builder.cylinderCollider(colliderRadius, colliderHeight);
        else if (colliderType == "capsule")
          builder.capsuleCollider(colliderRadius, colliderHeight);
        else if (colliderType == "cone")
          builder.coneCollider(colliderRadius, colliderHeight);
      }

      Entity builtEntity = builder.build(registry);
      if (hasAnimation && animComp.waypoints.size() >= 2)
        registry.addComponent<AnimationComponent>(builtEntity, animComp);
      continue;
    }

    if (line == "BeginSpawner") {
      std::string spawnerName = "Spawner";
      glm::vec3 position(0.0f);
      SpawnerComponent spawner;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndSpawner") break;

        if (line == "BeginTemplate") {
          SpawnTemplate tmpl;
          std::string tmplMeshRef, tmplMatRef;
          std::string tmplCollider = "sphere";

          while (std::getline(file, line)) {
            lineNum++;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line == "EndTemplate") break;

            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));

            if (key == "Weight")             tmpl.weight = parseFloat(val);
            else if (key == "Mesh")          tmplMeshRef = val;
            else if (key == "Material")      tmplMatRef = val;
            else if (key == "Scale")         tmpl.scale = parseVec3(val);
            else if (key == "ScaleUniform") {
              float s = parseFloat(val);
              tmpl.scale = glm::vec3(s);
            }
            else if (key == "NamePrefix")    tmpl.namePrefix = val;
            else if (key == "HasRender")     tmpl.hasRender = parseBool(val);
            else if (key == "Mass")          tmpl.simulated.mass = parseFloat(val);
            else if (key == "Restitution")   tmpl.simulated.restitution = parseFloat(val);
            else if (key == "Damping")       tmpl.simulated.damping = parseFloat(val);
            else if (key == "UseGravity")    tmpl.simulated.useGravity = parseBool(val);
            else if (key == "Velocity")      tmpl.simulated.velocity = parseVec3(val);
            else if (key == "AngularVelocity") tmpl.simulated.angularVelocity = parseVec3(val);
            else if (key == "Collider")      tmplCollider = val;
            else if (key == "ColliderRadius")      tmpl.collider.radius = parseFloat(val);
            else if (key == "ColliderHeight")      tmpl.collider.height = parseFloat(val);
            else if (key == "ColliderHalfExtents") tmpl.collider.halfExtents = parseVec3(val);
          }

          auto meshIt = namedMeshes.find(tmplMeshRef);
          auto matIt  = namedMaterials.find(tmplMatRef);
          if (meshIt != namedMeshes.end()) tmpl.meshID = meshIt->second;
          else tmpl.hasRender = false;
          if (matIt != namedMaterials.end()) tmpl.renderMaterialID = matIt->second;
          else tmpl.hasRender = false;

          if (tmplCollider == "sphere")         tmpl.collider.type = ColliderType::Sphere;
          else if (tmplCollider == "box")       tmpl.collider.type = ColliderType::AABB;
          else if (tmplCollider == "cylinder")  tmpl.collider.type = ColliderType::Cylinder;
          else if (tmplCollider == "capsule")   tmpl.collider.type = ColliderType::Capsule;
          else if (tmplCollider == "cone")      tmpl.collider.type = ColliderType::Cone;

          spawner.templates.push_back(tmpl);
          continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")                       spawnerName = val;
        else if (key == "Position")              position = parseVec3(val);
        else if (key == "SpawnInterval")         spawner.spawnInterval = parseFloat(val);
        else if (key == "SpawnIntervalRandomness") spawner.spawnIntervalRandomness = parseFloat(val);
        else if (key == "SpawnDirection")        spawner.spawnDirection = parseVec3(val);
        else if (key == "DirectionRandomness")   spawner.directionRandomness = parseFloat(val);
        else if (key == "SpawnSpeed")            spawner.spawnSpeed = parseFloat(val);
        else if (key == "SpeedRandomness")       spawner.speedRandomness = parseFloat(val);
        else if (key == "SpawnOffset")           spawner.spawnOffset = parseVec3(val);
        else if (key == "PositionRandomness")    spawner.positionRandomness = parseFloat(val);
        else if (key == "AngularVelocity")       spawner.angularVelocity = parseVec3(val);
        else if (key == "AngularVelocityRandomness") spawner.angularVelocityRandomness = parseFloat(val);
        else if (key == "MaxSpawns")             spawner.maxSpawns = parseInt(val);
        else if (key == "Enabled")               spawner.enabled = parseBool(val);
      }

      // Spawner entities carry a no-gravity SimulatedComponent so the network
      // ownership system can assign them to a specific peer.  No collider means
      // the physics library never touches them; they stay fixed.
      Entity entity = registry.createEntity();
      registry.addComponent<NameComponent>(entity, {spawnerName});
      TransformComponent tc;
      tc.position = position;
      registry.addComponent<TransformComponent>(entity, tc);
      SimulatedComponent pc;
      pc.useGravity = false;
      pc.damping    = 1.0f;
      registry.addComponent<SimulatedComponent>(entity, pc);
      registry.addComponent<SpawnerComponent>(entity, spawner);

      Debug::log(Debug::Category::OBJECTS, "WorldParser: Created spawner '",
                 spawnerName, "' with ", spawner.templates.size(), " template(s)");
      continue;
    }

    if (line == "BeginLight") {
      std::string lightName = "Unnamed Light";
      std::string lightType = "point";
      glm::vec3 position(0.0f);
      glm::vec3 direction(0.0f, -1.0f, 0.0f);
      glm::vec3 color(1.0f);
      float intensity = 1.0f;
      float constant = 1.0f;
      float linear = 0.09f;
      float quadratic = 0.032f;
      bool shadows = true;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndLight") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")
          lightName = val;
        else if (key == "Type")
          lightType = val;
        else if (key == "Position")
          position = parseVec3(val);
        else if (key == "Direction")
          direction = parseVec3(val);
        else if (key == "Color")
          color = parseVec3(val);
        else if (key == "Intensity")
          intensity = parseFloat(val);
        else if (key == "Constant")
          constant = parseFloat(val);
        else if (key == "Linear")
          linear = parseFloat(val);
        else if (key == "Quadratic")
          quadratic = parseFloat(val);
        else if (key == "CastsShadows")
          shadows = parseBool(val);
      }

      EntityBuilder builder;
      builder.name(lightName);
      builder.position(position);

      if (lightType == "sun") {
        builder.lightType(LightType::Sun);
        builder.direction(direction);
      } else {
        builder.lightType(LightType::Point);
      }

      builder.color(color);
      builder.intensity(intensity);
      builder.attenuation(constant, linear, quadratic);
      builder.castsShadows(shadows);
      builder.build(registry);
      continue;
    }

    if (line == "BeginCamera") {
      std::string camName = "Camera";
      glm::vec3 position(0.0f, 50.0f, 100.0f);
      glm::vec3 target(0.0f, 0.0f, 0.0f);
      bool hasTarget = false;
      CameraComponent cam;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndCamera") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "Name")             camName = val;
        else if (key == "Position")    position = parseVec3(val);
        else if (key == "Target")    { target = parseVec3(val); hasTarget = true; }
        else if (key == "FOV")         cam.fov = parseFloat(val);
        else if (key == "NearPlane")   cam.nearPlane = parseFloat(val);
        else if (key == "FarPlane")    cam.farPlane = parseFloat(val);
        else if (key == "OrthoSize")   cam.orthographicSize = parseFloat(val);
        else if (key == "Type") {
          if (val == "orthographic" || val == "Orthographic")
            cam.type = CameraType::Orthographic;
          else
            cam.type = CameraType::Perspective;
        }
      }

      Entity camEntity = registry.createEntity();
      registry.addComponent<NameComponent>(camEntity, {camName});

      TransformComponent t;
      t.position = position;
      t.scale = glm::vec3(1.0f);
      if (hasTarget) {
        glm::vec3 fwd = glm::normalize(target - position);
        glm::vec3 up(0, 1, 0);
        if (std::abs(glm::dot(fwd, up)) > 0.999f) up = glm::vec3(0, 0, 1);
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        glm::vec3 newUp = glm::cross(right, fwd);
        t.rotation = glm::quat_cast(glm::mat3(right, newUp, -fwd));
      }
      registry.addComponent<TransformComponent>(camEntity, t);
      registry.addComponent<CameraComponent>(camEntity, cam);

      Debug::log(Debug::Category::MAIN, "WorldParser: Created camera '", camName, "'");
      continue;
    }

    if (line == "BeginCloth") {
      std::string clothName = "Cloth";
      glm::vec3 position(0.0f, 10.0f, 0.0f);
      std::string materialRef;
      ClothComponent cloth;

      while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "EndCloth") break;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "Name")          clothName = val;
        else if (key == "Position")      position = parseVec3(val);
        else if (key == "Material")      materialRef = val;
        else if (key == "ResolutionX")   cloth.resolutionX = parseInt(val);
        else if (key == "ResolutionZ")   cloth.resolutionZ = parseInt(val);
        else if (key == "Width")         cloth.width = parseFloat(val);
        else if (key == "ClothHeight")   cloth.clothHeight = parseFloat(val);
        else if (key == "StructuralStiffness") cloth.structuralStiffness = parseFloat(val);
        else if (key == "BendingStiffness")    cloth.bendingStiffness = parseFloat(val);
        else if (key == "Stiffness") {
          float s = parseFloat(val);
          cloth.structuralStiffness = s;
          cloth.bendingStiffness    = s * 0.15f;
        }
        else if (key == "Damping")         cloth.damping = parseFloat(val);
        else if (key == "ParticleMass")    cloth.particleMass = parseFloat(val);
        else if (key == "UseGravity")      cloth.useGravity = parseBool(val);
        else if (key == "EulerAngles")     cloth.eulerAngles = parseVec3(val);
        else if (key == "SolverIterations") cloth.solverIterations = parseInt(val);
        else if (key == "PinSpacing")      cloth.pinSpacing = parseInt(val);
        else if (key == "Tearability")     cloth.tearability = parseFloat(val);
        else if (key == "TearThreshold")   cloth.tearability = parseFloat(val);
        else if (key == "Hinge") {
          if      (val == "None")          cloth.hinge = ClothHinge::None;
          else if (val == "TopRow")        cloth.hinge = ClothHinge::TopRow;
          else if (val == "BottomRow")     cloth.hinge = ClothHinge::BottomRow;
          else if (val == "LeftCol")       cloth.hinge = ClothHinge::LeftCol;
          else if (val == "RightCol")      cloth.hinge = ClothHinge::RightCol;
          else if (val == "TopCorners")    cloth.hinge = ClothHinge::TopCorners;
          else if (val == "BottomCorners") cloth.hinge = ClothHinge::BottomCorners;
          else if (val == "AllCorners")    cloth.hinge = ClothHinge::AllCorners;
        }
        else if (key == "PinnedTopRow") {
          cloth.hinge = parseBool(val) ? ClothHinge::TopRow : ClothHinge::None;
        }
        else if (key == "Wind")            cloth.wind = parseVec3(val);
        else if (key == "OwnerPeer")       cloth.ownerPeerId = static_cast<uint8_t>(parseInt(val));
      }

      Entity clothEntity = registry.createEntity();
      registry.addComponent<NameComponent>(clothEntity, {clothName});
      TransformComponent t;
      t.position = position;
      registry.addComponent<TransformComponent>(clothEntity, t);
      registry.addComponent<RenderComponent>(clothEntity, RenderComponent{});
      registry.addComponent<ClothComponent>(clothEntity, cloth);

      auto matIt = namedMaterials.find(materialRef);
      if (matIt != namedMaterials.end())
        registry.addComponent<RenderMaterialComponent>(clothEntity,
                                                       {matIt->second});

      Debug::log(Debug::Category::MAIN, "WorldParser: Created cloth '", clothName, "'");
      continue;
    }
  }

  Debug::log(Debug::Category::MAIN, "WorldParser: Finished loading world: ",
             filepath);
  return true;
}

std::string WorldParser::trim(const std::string& str) const {
  const size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, last - first + 1);
}

glm::vec3 WorldParser::parseVec3(const std::string& value) const {
  glm::vec3 result(0.0f);
  std::istringstream ss(value);
  char comma;
  ss >> result.x >> comma >> result.y >> comma >> result.z;
  return result;
}

glm::vec4 WorldParser::parseVec4(const std::string& value) const {
  glm::vec4 result(0.0f);
  std::istringstream ss(value);
  char comma;
  ss >> result.x >> comma >> result.y >> comma >> result.z >> comma >> result.w;
  return result;
}

float WorldParser::parseFloat(const std::string& value) const {
  try {
    return std::stof(value);
  } catch (...) {
    return 0.0f;
  }
}

int WorldParser::parseInt(const std::string& value) const {
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

bool WorldParser::parseBool(const std::string& value) const {
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), ::tolower);
  return v == "true" || v == "1" || v == "yes";
}
