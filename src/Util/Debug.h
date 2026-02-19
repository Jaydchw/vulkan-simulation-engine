#pragma once
#include <iostream>

class Debug final {
 public:
  enum class Category {
    MAIN,
    CAMERA,
    INPUT,
    RENDERING,
    VULKAN,
    SKYBOX,
    PLANTMANAGER,
    WORLD,
    PARTICLES,
    MESH,
    LIGHTS,
    SCENE,
    OBJECTS,
    TEXTURE,
    MATERIALS,
    POSTPROCESSING,
    SHADOWS
  };

  static void setEnabled(Category category, bool enabled) {
    switch (category) {
      case Category::MAIN:
        enableMain = enabled;
        break;
      case Category::CAMERA:
        enableCamera = enabled;
        break;
      case Category::INPUT:
        enableInput = enabled;
        break;
      case Category::RENDERING:
        enableRendering = enabled;
        break;
      case Category::VULKAN:
        enableVulkan = enabled;
        break;
      case Category::SKYBOX:
        enableSkybox = enabled;
        break;
      case Category::PLANTMANAGER:
        enablePlantManager = enabled;
        break;
      case Category::WORLD:
        enableWorld = enabled;
        break;
      case Category::PARTICLES:
        enableParticles = enabled;
        break;
      case Category::MESH:
        enableMesh = enabled;
        break;
      case Category::LIGHTS:
        enableLights = enabled;
        break;
      case Category::SCENE:
        enableScene = enabled;
        break;
      case Category::OBJECTS:
        enableObjects = enabled;
        break;
      case Category::TEXTURE:
        enableTexture = enabled;
        break;
      case Category::MATERIALS:
        enableMaterials = enabled;
        break;
      case Category::POSTPROCESSING:
        enablePostProcessing = enabled;
        break;
      case Category::SHADOWS:
        enableShadows = enabled;
        break;
    }
  }

  static bool isEnabled(Category category) {
    switch (category) {
      case Category::MAIN:
        return enableMain;
      case Category::CAMERA:
        return enableCamera;
      case Category::INPUT:
        return enableInput;
      case Category::RENDERING:
        return enableRendering;
      case Category::VULKAN:
        return enableVulkan;
      case Category::SKYBOX:
        return enableSkybox;
      case Category::PLANTMANAGER:
        return enablePlantManager;
      case Category::WORLD:
        return enableWorld;
      case Category::PARTICLES:
        return enableParticles;
      case Category::MESH:
        return enableMesh;
      case Category::LIGHTS:
        return enableLights;
      case Category::SCENE:
        return enableScene;
      case Category::OBJECTS:
        return enableObjects;
      case Category::TEXTURE:
        return enableTexture;
      case Category::MATERIALS:
        return enableMaterials;
      case Category::POSTPROCESSING:
        return enablePostProcessing;
      case Category::SHADOWS:
        return enableShadows;
      default:
        return false;
    }
  }

  template <typename... Args>
  static void log(Category category, Args&&... args) {
    if (isEnabled(category)) {
      std::cout << "[" << categoryName(category) << "] ";
      (std::cout << ... << args) << std::endl;
    }
  }

 private:
  static const char* categoryName(Category cat) {
    switch (cat) {
      case Category::MAIN:
        return "MAIN";
      case Category::CAMERA:
        return "CAMERA";
      case Category::INPUT:
        return "INPUT";
      case Category::RENDERING:
        return "RENDERING";
      case Category::VULKAN:
        return "VULKAN";
      case Category::SKYBOX:
        return "SKYBOX";
      case Category::PLANTMANAGER:
        return "PLANTMANAGER";
      case Category::WORLD:
        return "WORLD";
      case Category::PARTICLES:
        return "PARTICLES";
      case Category::MESH:
        return "MESH";
      case Category::LIGHTS:
        return "LIGHTS";
      case Category::SCENE:
        return "SCENE";
      case Category::OBJECTS:
        return "OBJECTS";
      case Category::TEXTURE:
        return "TEXTURE";
      case Category::MATERIALS:
        return "MATERIALS";
      case Category::POSTPROCESSING:
        return "POSTPROCESSING";
      case Category::SHADOWS:
        return "SHADOWS";
      default:
        return "UNKNOWN";
    }
  }

  static bool enableMain;
  static bool enableCamera;
  static bool enableInput;
  static bool enableRendering;
  static bool enableVulkan;
  static bool enableSkybox;
  static bool enablePlantManager;
  static bool enableWorld;
  static bool enableParticles;
  static bool enableMesh;
  static bool enableLights;
  static bool enableScene;
  static bool enableObjects;
  static bool enableTexture;
  static bool enableMaterials;
  static bool enablePostProcessing;
  static bool enableShadows;
};

inline bool Debug::enableMain = false;
inline bool Debug::enableCamera = false;
inline bool Debug::enableInput = false;
inline bool Debug::enableRendering = false;
inline bool Debug::enableVulkan = false;
inline bool Debug::enableSkybox = false;
inline bool Debug::enablePlantManager = false;
inline bool Debug::enableWorld = false;
inline bool Debug::enableParticles = false;
inline bool Debug::enableMesh = false;
inline bool Debug::enableLights = false;
inline bool Debug::enableScene = false;
inline bool Debug::enableObjects = false;
inline bool Debug::enableTexture = false;
inline bool Debug::enableMaterials = false;
inline bool Debug::enablePostProcessing = false;
inline bool Debug::enableShadows = false;

