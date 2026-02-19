#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "Application.h"
#include "Resources/Object.h"
#include "Util/ConfigParser.h"
#include "Util/Debug.h"

void printControls() {
  std::cout << R"(
CONTROLS
ESC - Exit
W/A/S/D - Move Camera
L - Toggle Shading Mode
K - Toggle Toon Shader
)" << std::endl;
}

void createScene(MeshManager* meshManager, MaterialManager* materialManager,
                 LightManager* lightManager,
                 std::vector<Object>& sceneObjects) {
  Debug::log(Debug::Category::MAIN, "Creating simple scene...");
  const MaterialID floorMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Floor")
                                            .albedoColor(0.5f, 0.5f, 0.5f)
                                            .roughness(0.8f)
                                            .metallic(0.0f));
  const MaterialID cubeMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Cube Material")
                                            .albedoColor(0.8f, 0.2f, 0.2f)
                                            .roughness(0.4f)
                                            .metallic(0.1f));
  const MeshID floorMesh = meshManager->createPlane(50.0f, 50.0f);
  const MeshID cubeMesh = meshManager->createCube(2.0f);
  Object floorObj;
  ObjectBuilder()
      .name("Floor")
      .position(0.0f, 0.0f, 0.0f)
      .mesh(floorMesh)
      .material(floorMat)
      .build(floorObj);
  sceneObjects.push_back(floorObj);
  Object cubeObj;
  ObjectBuilder()
      .name("Cube")
      .position(0.0f, 1.0f, 0.0f)
      .mesh(cubeMesh)
      .material(cubeMat)
      .build(cubeObj);
  sceneObjects.push_back(cubeObj);
  Light sunLight;
  LightBuilder()
      .type(LightType::Sun)
      .name("Sun Light")
      .direction(0.5f, -1.0f, 0.5f)
      .color(1.0f, 0.95f, 0.8f)
      .intensity(2.0f)
      .castsShadows(true)
      .build(sunLight);
  lightManager->addLight(sunLight);
  Light pointLight;
  LightBuilder()
      .type(LightType::Point)
      .name("Blue Light")
      .position(5.0f, 3.0f, 5.0f)
      .color(0.2f, 0.2f, 1.0f)
      .intensity(5.0f)
      .attenuation(1.0f, 0.09f, 0.032f)
      .castsShadows(true)
      .build(pointLight);
  lightManager->addLight(pointLight);
}

int main() {
#ifdef _DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
  ConfigParser config;
  config.load("config.ini");
  Debug::setEnabled(Debug::Category::MAIN, true);
  Debug::setEnabled(Debug::Category::VULKAN, true);
  Debug::setEnabled(Debug::Category::RENDERING, true);
  if (Debug::isEnabled(Debug::Category::MAIN)) printControls();
  try {
    Application app;
    Debug::log(Debug::Category::MAIN,
               "Initializing Vulkan Simulation Engine...");
    app.init();
    std::vector<Object> sceneObjects;
    createScene(app.getMeshManager(), app.getMaterialManager(),
                app.getLightManager(), sceneObjects);
    app.setScene(sceneObjects);
    app.getLightManager()->debugPrintLightInfo();
    Debug::log(Debug::Category::MAIN, "Starting main loop...");
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}