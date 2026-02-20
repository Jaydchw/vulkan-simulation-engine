#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "Application.h"
#include "ECS/EntityBuilder.h"
#include "ECS/Registry.h"
#include "Resources/ProceduralTexture.h"
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
                 TextureManager* textureManager, Registry& registry) {
  Debug::log(Debug::Category::MAIN, "Creating scene...");

  const TextureID checkerTex = ProceduralTexture::checker(
      textureManager, glm::vec3(0.35f, 0.35f, 0.35f),
      glm::vec3(0.65f, 0.65f, 0.65f), 512, 16);
  const MaterialID floorMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Checker Floor")
                                            .albedoMap(checkerTex)
                                            .roughness(0.9f)
                                            .metallic(0.0f)
                                            .textureScale(4.0f));

  const MaterialID redMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Red")
                                            .albedoColor(0.9f, 0.15f, 0.15f)
                                            .roughness(0.3f)
                                            .metallic(0.0f));

  const MaterialID greenMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Green")
                                            .albedoColor(0.15f, 0.8f, 0.2f)
                                            .roughness(0.5f)
                                            .metallic(0.0f));

  const MaterialID blueMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Blue")
                                            .albedoColor(0.2f, 0.3f, 0.9f)
                                            .roughness(0.4f)
                                            .metallic(0.1f));

  const TextureID goldTex =
      ProceduralTexture::solid(textureManager, glm::vec3(1.0f, 0.84f, 0.0f));
  const MaterialID goldMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Gold")
                                            .albedoMap(goldTex)
                                            .roughness(0.2f)
                                            .metallic(0.9f));

  const TextureID gradientTex = ProceduralTexture::linearGradient(
      textureManager, glm::vec3(0.9f, 0.4f, 0.1f),
      glm::vec3(0.6f, 0.1f, 0.8f), 256, true);
  const MaterialID gradientMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Sunset Gradient")
                                            .albedoMap(gradientTex)
                                            .roughness(0.6f)
                                            .metallic(0.0f));

  const TextureID stripeTex = ProceduralTexture::stripe(
      textureManager, glm::vec3(0.1f, 0.1f, 0.1f),
      glm::vec3(0.95f, 0.95f, 0.0f), 256, 6, true);
  const MaterialID stripeMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Warning Stripes")
                                            .albedoMap(stripeTex)
                                            .roughness(0.5f)
                                            .metallic(0.0f));

  const TextureID radialTex = ProceduralTexture::radialGradient(
      textureManager, glm::vec3(1.0f, 1.0f, 1.0f),
      glm::vec3(0.05f, 0.05f, 0.3f), 256);
  const MaterialID radialMat =
      materialManager->registerMaterial(MaterialBuilder()
                                            .name("Radial Glow")
                                            .albedoMap(radialTex)
                                            .roughness(0.7f)
                                            .metallic(0.0f));

  const MeshID floorMesh = meshManager->createPlane(50.0f, 50.0f);
  const MeshID cubeMesh = meshManager->createCube(2.0f);
  const MeshID sphereMesh = meshManager->createSphere(1.0f, 32);
  const MeshID cylinderMesh = meshManager->createCylinder(0.8f, 2.5f, 32);

  EntityBuilder()
      .name("Floor")
      .position(0.0f, 0.0f, 0.0f)
      .mesh(floorMesh)
      .material(floorMat)
      .build(registry);

  EntityBuilder()
      .name("Red Cube")
      .position(-6.0f, 1.0f, 0.0f)
      .mesh(cubeMesh)
      .material(redMat)
      .build(registry);

  EntityBuilder()
      .name("Green Cube")
      .position(-3.0f, 1.0f, 0.0f)
      .mesh(cubeMesh)
      .material(greenMat)
      .build(registry);

  EntityBuilder()
      .name("Blue Cube")
      .position(0.0f, 1.0f, 0.0f)
      .mesh(cubeMesh)
      .material(blueMat)
      .build(registry);

  EntityBuilder()
      .name("Gold Sphere")
      .position(3.0f, 1.0f, 0.0f)
      .mesh(sphereMesh)
      .material(goldMat)
      .build(registry);

  EntityBuilder()
      .name("Gradient Sphere")
      .position(6.0f, 1.0f, 0.0f)
      .mesh(sphereMesh)
      .material(gradientMat)
      .build(registry);

  EntityBuilder()
      .name("Striped Cylinder")
      .position(-4.0f, 1.25f, 5.0f)
      .mesh(cylinderMesh)
      .material(stripeMat)
      .build(registry);

  EntityBuilder()
      .name("Radial Sphere")
      .position(0.0f, 1.0f, 5.0f)
      .mesh(sphereMesh)
      .material(radialMat)
      .build(registry);

  EntityBuilder()
      .name("Checker Cube")
      .position(4.0f, 1.0f, 5.0f)
      .rotationEuler(0.0f, 45.0f, 0.0f)
      .mesh(cubeMesh)
      .material(
          materialManager->registerMaterial(
              MaterialBuilder()
                  .name("Checker Cube Mat")
                  .albedoMap(ProceduralTexture::checker(
                      textureManager, glm::vec3(0.1f, 0.1f, 0.9f),
                      glm::vec3(0.9f, 0.9f, 0.9f), 256, 4))
                  .roughness(0.3f)
                  .metallic(0.0f)))
      .build(registry);

  EntityBuilder()
      .name("Sun Light")
      .lightType(LightType::Sun)
      .direction(0.5f, -1.0f, 0.5f)
      .color(1.0f, 0.95f, 0.8f)
      .intensity(2.0f)
      .castsShadows(true)
      .build(registry);

  EntityBuilder()
      .name("Blue Point Light")
      .lightType(LightType::Point)
      .position(5.0f, 3.0f, 5.0f)
      .color(0.2f, 0.2f, 1.0f)
      .intensity(5.0f)
      .attenuation(1.0f, 0.09f, 0.032f)
      .castsShadows(true)
      .build(registry);
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
    Registry registry;
    createScene(app.getMeshManager(), app.getMaterialManager(),
                app.getTextureManager(), registry);
    app.setRegistry(registry);
    app.getLightManager()->debugPrintLightInfo();
    Debug::log(Debug::Category::MAIN, "Starting main loop...");
    app.run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}