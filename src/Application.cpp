#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Application.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>

#include "Util/Debug.h"
#include "Util/RenderUtils.h"
#include "Physics/PhysicsSystem.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanDepthBuffer.h"
#include "Vulkan/VulkanDescriptors.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/VulkanSyncObjects.h"

Application::Application() {
  Debug::log(Debug::Category::MAIN, "Application: Constructor called");
}

Application::~Application() {
  try {
    Debug::log(Debug::Category::MAIN, "Application: Destructor called");
  } catch (...) {
  }
}

void Application::init() {
  initWindow();
  initVulkan();
}

void Application::setRegistry(Registry& reg) {
  registry = &reg;
  lightManager->setRegistry(registry);
  lightManager->syncLights();
  physicsSystem->setRegistry(registry);
  physicsSystem->saveInitialSnapshot();
}

void Application::loadWorld(const std::string& filepath) {
vkDeviceWaitIdle(device);

if (interface) interface->clearSelection();

ownedRegistry = std::make_unique<Registry>();
  registry = ownedRegistry.get();

  WorldSettings worldSettings;
  worldParser->load(filepath, *registry, worldSettings);

  sceneSettings.clearColor = worldSettings.clearColor;

  lightManager->setRegistry(registry);
  lightManager->syncLights();

  physicsSystem->setRegistry(registry);
  physicsSystem->saveInitialSnapshot();

  simState = SimulationState{};
  simState.timeSpeed = worldSettings.timeSpeed;

  Debug::log(Debug::Category::MAIN, "Application: Loaded world: ", filepath);
}

void Application::run() {
  mainLoop();
  cleanup();
}

void Application::initWindow() {
  window = std::make_unique<Window>(WIDTH, HEIGHT, "Vulkan Simulation Engine");
  window->setUserPointer(this);
  window->setFramebufferSizeCallback(framebufferResizeCallback);
  window->setKeyCallback(keyCallback);
  window->setCursorPosCallback(cursorPosCallback);
  window->setMouseButtonCallback(mouseButtonCallback);
  window->setScrollCallback(scrollCallback);
  lastFrameTime = static_cast<float>(glfwGetTime());
}

void Application::initVulkan() {
  instance = Vulkan::createInstance();
  debugMessenger = Vulkan::setupDebugMessenger(instance);
  surface = window->createSurface(instance);
  physicalDevice = Vulkan::pickPhysicalDevice(instance, surface);
  device = Vulkan::createLogicalDevice(physicalDevice, surface, graphicsQueue,
                                       presentQueue);
  swapChain = Vulkan::createSwapChain(device, physicalDevice, surface,
                                      window->getHandle(), swapChainImageFormat,
                                      swapChainExtent, swapChainImages);
  Vulkan::createImageViews(device, swapChainImages, swapChainImageFormat,
                           swapChainImageViews);
  depthFormat = Vulkan::findDepthFormat(physicalDevice);
  descriptorSetLayout = Vulkan::createDescriptorSetLayout(device);
  materialDescriptorSetLayout =
      Vulkan::createMaterialDescriptorSetLayout(device);
  commandPool = Vulkan::createCommandPool(device, physicalDevice, surface);

  Vulkan::QueueFamilyIndices indices;
  Vulkan::findQueueFamilies(physicalDevice, surface, indices);

  interface = std::make_unique<Interface>(
      window->getHandle(), instance, physicalDevice, device, graphicsQueue,
      commandPool,  // Passed CommandPool here
      indices.graphicsFamily.value(), swapChainImageFormat, depthFormat);
  interface->init();
  interface->resize(swapChainExtent, swapChainImageViews);

  renderDevice = std::make_unique<RenderDevice>(device, physicalDevice,
                                                commandPool, graphicsQueue);
  textureManager = std::make_unique<TextureManager>(device, physicalDevice,
                                                    commandPool, graphicsQueue);
  materialManager = std::make_unique<MaterialManager>(renderDevice.get(),
                                                      textureManager.get());
  meshManager = std::make_unique<MeshManager>(renderDevice.get());
  gizmoMeshID = meshManager->createSphere(0.3f, 16);
  lightManager = std::make_unique<LightManager>(renderDevice.get());
  descriptorPool = Vulkan::createDescriptorPool(device, MAX_FRAMES_IN_FLIGHT);
  materialManager->init(materialDescriptorSetLayout, descriptorPool);
  gizmoMaterialID = materialManager->getDefaultMaterial();
  lightManager->init();
  mainPipeline =
      std::make_unique<MainPipeline>(device, swapChainImageFormat, depthFormat);
  mainPipeline->create(descriptorSetLayout, materialDescriptorSetLayout,
                       lightManager->getShadowDescriptorSetLayout());
  createShadowPipeline();
  postProcessing = std::make_unique<PostProcessing>(renderDevice.get(), device,
                                                    swapChainImageFormat);
  postProcessing->init(descriptorPool, swapChainExtent.width,
                       swapChainExtent.height);
  createDepthResources();
  createUniformBuffers();
  Vulkan::createDescriptorSets(device, descriptorPool, descriptorSetLayout,
                               uniformBuffers, lightManager->getLightBuffer(),
                               MAX_FRAMES_IN_FLIGHT, descriptorSets);
  Vulkan::createCommandBuffers(device, commandPool, MAX_FRAMES_IN_FLIGHT,
                               commandBuffers);
  Vulkan::createSyncObjects(device, static_cast<int>(swapChainImages.size()),
                            imageAvailableSemaphores, renderFinishedSemaphores,
                            inFlightFences);

  worldParser = std::make_unique<WorldParser>(meshManager.get(),
                                              materialManager.get(),
                                              textureManager.get());
  physicsSystem = std::make_unique<PhysicsSystem>();
  interface->setWorldDirectory("Worlds");
  interface->setWorldLoadCallback(
      [this](const std::string& path) { loadWorld(path); });
}

void Application::mainLoop() {
setCameraPreset(1);
while (!window->shouldClose()) {
  window->pollEvents();
  const float currentTime = static_cast<float>(glfwGetTime());
  const float deltaTime = currentTime - lastFrameTime;
  lastFrameTime = currentTime;

  if (simState.resetRequested) {
    simState.resetRequested = false;
    physicsSystem->restoreInitialSnapshot();
    physicsSystem->clearSnapshots();
    simState.timeHistory.clear();
    simState.currentTime = 0.0f;
    simState.historyIndex = -1;
    simState.rewinding = false;
    simState.reversePlay = false;
  }

  if (simState.snapshotScrubbed) {
    simState.snapshotScrubbed = false;
    if (simState.historyIndex >= 0 &&
        simState.historyIndex < physicsSystem->getSnapshotCount()) {
      physicsSystem->restoreSnapshot(simState.historyIndex);
    }
  }

  if (simState.reversePlay && !simState.isPaused) {
    int snapshotCount = physicsSystem->getSnapshotCount();
    if (snapshotCount > 0) {
      if (simState.historyIndex < 0)
        simState.historyIndex = snapshotCount - 1;

      float rewindSpeed = deltaTime * simState.timeSpeed;
      float framesBack = rewindSpeed / simState.stepSize;
      int steps = glm::max(static_cast<int>(framesBack), 1);

      simState.historyIndex = glm::max(simState.historyIndex - steps, 0);
      physicsSystem->restoreSnapshot(simState.historyIndex);

      if (simState.historyIndex < static_cast<int>(simState.timeHistory.size()))
        simState.currentTime = simState.timeHistory[simState.historyIndex];

      if (simState.historyIndex <= 0) {
        simState.reversePlay = false;
        simState.isPaused = true;
      }
    }
  } else if (!simState.isPaused || simState.stepFrame) {
    if (!physicsSystem->hasInitialSnapshot()) {
      physicsSystem->saveInitialSnapshot();
    }

    if (simState.historyIndex >= 0) {
      int truncIdx = simState.historyIndex;
      physicsSystem->truncateAfter(truncIdx);
      if (truncIdx + 1 < static_cast<int>(simState.timeHistory.size()))
        simState.timeHistory.erase(
            simState.timeHistory.begin() + truncIdx + 1,
            simState.timeHistory.end());
      simState.historyIndex = -1;
    }

    float advance = simState.stepFrame ? simState.stepSize
                                       : deltaTime * simState.timeSpeed;
    simState.currentTime += advance;
    simState.stepFrame = false;
    simState.rewinding = false;
    simState.reversePlay = false;

    physicsSystem->update(advance);
    physicsSystem->saveSnapshot();
    simState.timeHistory.push_back(simState.currentTime);

    while (static_cast<int>(simState.timeHistory.size()) >
           physicsSystem->getSnapshotCount()) {
      simState.timeHistory.erase(simState.timeHistory.begin());
    }
  }

    interface->render(simState, sceneSettings, *registry, mainPipeline.get(),
                       postProcessing.get());

    if (!ImGui::GetIO().WantCaptureMouse &&
        !ImGui::GetIO().WantCaptureKeyboard) {
      input.update();
      camera.update(input, deltaTime);
      camera.setCursorMode(window->getHandle());
    } else {
      glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    input.endFrame();
    drawFrame();
  }
  vkDeviceWaitIdle(device);
}

void Application::cleanup() {
  vkDeviceWaitIdle(device);
  cleanupSwapChain();
  interface->cleanup();
  postProcessing.reset();
  lightManager.reset();
  materialManager.reset();
  textureManager.reset();
  renderDevice.reset();
  meshManager.reset();
  if (shadowPipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(device, shadowPipeline, nullptr);
  if (shadowPipelineLayout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
  if (mainPipeline) mainPipeline->cleanup();
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout, nullptr);
  vkDestroyBuffer(device, indexBuffer, nullptr);
  vkFreeMemory(device, indexBufferMemory, nullptr);
  vkDestroyBuffer(device, vertexBuffer, nullptr);
  vkFreeMemory(device, vertexBufferMemory, nullptr);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroyBuffer(device, uniformBuffers[i], nullptr);
    vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
  }
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }
  vkDestroyCommandPool(device, commandPool, nullptr);
  vkDestroyDevice(device, nullptr);
  if (Vulkan::enableValidationLayers && debugMessenger != VK_NULL_HANDLE)
    Vulkan::DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
}

void Application::createUniformBuffers() {
  const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
  uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
  uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    renderDevice->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               uniformBuffers[i], uniformBuffersMemory[i]);
    vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0,
                &uniformBuffersMapped[i]);
  }
}

void Application::drawFrame() {
  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                  UINT64_MAX);
  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
      VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    throw std::runtime_error("Failed to acquire swap chain image!");
  vkResetFences(device, 1, &inFlightFences[currentFrame]);
  updateUniformBuffer(currentFrame);
  vkResetCommandBuffer(commandBuffers[currentFrame], 0);
  recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;
  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                    inFlightFences[currentFrame]) != VK_SUCCESS)
    throw std::runtime_error("Failed to submit draw command buffer!");
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  VkSwapchainKHR swapChains[] = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;
  result = vkQueuePresentKHR(presentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (result != VK_SUCCESS)
    throw std::runtime_error("Failed to present swap chain image!");
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::recreateSwapChain() {
  int width = 0, height = 0;
  window->getFramebufferSize(width, height);
  while (width == 0 || height == 0) {
    window->getFramebufferSize(width, height);
    window->waitEvents();
  }
  vkDeviceWaitIdle(device);
  cleanupSwapChain();
  swapChain = Vulkan::createSwapChain(device, physicalDevice, surface,
                                      window->getHandle(), swapChainImageFormat,
                                      swapChainExtent, swapChainImages);
  Vulkan::createImageViews(device, swapChainImages, swapChainImageFormat,
                           swapChainImageViews);
  createDepthResources();
  postProcessing->resize(swapChainExtent.width, swapChainExtent.height,
                         descriptorPool);
  interface->resize(swapChainExtent, swapChainImageViews);
}

void Application::cleanupSwapChain() {
  if (depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, depthImageView, nullptr);
    depthImageView = VK_NULL_HANDLE;
  }
  if (depthImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, depthImage, nullptr);
    depthImage = VK_NULL_HANDLE;
  }
  if (depthImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, depthImageMemory, nullptr);
    depthImageMemory = VK_NULL_HANDLE;
  }
  for (const auto imageView : swapChainImageViews)
    vkDestroyImageView(device, imageView, nullptr);
  vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void Application::updateUniformBuffer(uint32_t currentImage) {
  const glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
  const float sceneRadius = 100.0f;
  lightManager->updateAllShadowMatrices(sceneCenter, sceneRadius);
  lightManager->updateLightBuffer();
  UniformBufferObject ubo{};
  ubo.view = camera.getViewMatrix();
  ubo.proj = glm::perspective(
      glm::radians(45.0f),
      swapChainExtent.width / static_cast<float>(swapChainExtent.height), 0.1f,
      50000.0f);
  ubo.proj[1][1] *= -1;
  ubo.eyePos = camera.getPosition();
  ubo.time = simState.currentTime;
  std::vector<ShadowMapData> shadowMaps;
  lightManager->getShadowSystem()->getShadowMaps(shadowMaps);
  for (size_t i = 0; i < shadowMaps.size() && i < MAX_SHADOW_CASTERS; i++)
    ubo.lightSpaceMatrices[i] = shadowMaps[i].lightSpaceMatrix;
  for (size_t i = shadowMaps.size(); i < MAX_SHADOW_CASTERS; i++)
    ubo.lightSpaceMatrices[i] = glm::mat4(1.0f);
  memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void Application::recreateGraphicsPipeline() {
  vkDeviceWaitIdle(device);
  mainPipeline->recreate();
}

void Application::recreateTextureSamplers(VkFilter magFilter,
                                          VkFilter minFilter) {
  vkDeviceWaitIdle(device);
  textureManager->recreateSamplers(magFilter, minFilter);
}

void Application::toggleShadingMode() {
  if (mainPipeline->getShadingMode() == MainPipeline::ShadingMode::Phong)
    mainPipeline->setShadingMode(MainPipeline::ShadingMode::Gouraud);
  else
    mainPipeline->setShadingMode(MainPipeline::ShadingMode::Phong);
  recreateGraphicsPipeline();
}

void Application::createDepthResources() {
  RenderUtils::createImageWithMemory(
      device, physicalDevice, swapChainExtent.width, swapChainExtent.height,
      depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
  depthImageView = RenderUtils::createImageView(device, depthImage, depthFormat,
                                                VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Application::framebufferResizeCallback(GLFWwindow* win, int width,
                                            int height) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->framebufferResized = true;
}

void Application::keyCallback(GLFWwindow* win, int key, int scancode,
                              int action, int mods) {
  if (win == nullptr) return;
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onKey(key, scancode, action, mods);
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, true);
        break;
      case GLFW_KEY_R:
        app->simState.resetRequested = true;
        break;
      case GLFW_KEY_SPACE:
        if (!ImGui::GetIO().WantCaptureKeyboard) {
          app->simState.isPaused = !app->simState.isPaused;
          app->simState.rewinding = false;
          app->simState.reversePlay = false;
        }
        break;
      case GLFW_KEY_PERIOD:
        app->simState.isPaused = true;
        app->simState.stepFrame = true;
        app->simState.rewinding = false;
        break;
      case GLFW_KEY_COMMA:
        if (app->physicsSystem->getSnapshotCount() > 0) {
          app->simState.isPaused = true;
          app->simState.rewinding = true;
          app->simState.reversePlay = false;
          int snapshotCount = app->physicsSystem->getSnapshotCount();
          if (app->simState.historyIndex < 0)
            app->simState.historyIndex = snapshotCount - 1;
          if (app->simState.historyIndex > 0) {
            app->simState.historyIndex--;
            app->physicsSystem->restoreSnapshot(app->simState.historyIndex);
            if (app->simState.historyIndex <
                static_cast<int>(app->simState.timeHistory.size()))
              app->simState.currentTime =
                  app->simState.timeHistory[app->simState.historyIndex];
          }
        }
        break;
      case GLFW_KEY_EQUAL:
      case GLFW_KEY_KP_ADD:
        app->simState.timeSpeed =
            glm::min(app->simState.timeSpeed * 2.0f, 10.0f);
        break;
      case GLFW_KEY_MINUS:
      case GLFW_KEY_KP_SUBTRACT:
        app->simState.timeSpeed =
            glm::max(app->simState.timeSpeed * 0.5f, 0.01f);
        break;
      case GLFW_KEY_L:
        app->toggleShadingMode();
        break;
      case GLFW_KEY_K:
        app->postProcessing->toggleToonMode();
        break;
      case GLFW_KEY_TAB: {
        auto current = app->mainPipeline->getPolygonMode();
        if (current == VK_POLYGON_MODE_FILL)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_LINE);
        else if (current == VK_POLYGON_MODE_LINE)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_POINT);
        else
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_FILL);
        app->recreateGraphicsPipeline();
        break;
      }
      case GLFW_KEY_G:
        if (app->mainPipeline->getPolygonMode() == VK_POLYGON_MODE_LINE)
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_FILL);
        else
          app->mainPipeline->setPolygonMode(VK_POLYGON_MODE_LINE);
        app->recreateGraphicsPipeline();
        break;
      case GLFW_KEY_F1:
        break;
      case GLFW_KEY_F11: {
        GLFWmonitor* monitor = glfwGetWindowMonitor(win);
        if (monitor) {
          glfwSetWindowMonitor(win, nullptr, 100, 100, WIDTH, HEIGHT, 0);
        } else {
          monitor = glfwGetPrimaryMonitor();
          const GLFWvidmode* mode = glfwGetVideoMode(monitor);
          glfwSetWindowMonitor(win, monitor, 0, 0, mode->width, mode->height,
                               mode->refreshRate);
        }
        break;
      }
      case GLFW_KEY_1:
        app->setCameraPreset(1);
        break;
      case GLFW_KEY_2:
        app->setCameraPreset(2);
        break;
      default:
        break;
    }
  }
}

void Application::setCameraPreset(int presetIndex) {
  if (presetIndex == 1)
    camera.setPose(glm::vec3(0.0f, 50.0f, 100.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  else if (presetIndex == 2)
    camera.setPose(glm::vec3(50.0f, 30.0f, 50.0f),
                   glm::vec3(0.0f, 10.0f, 0.0f));
}

void Application::resetApplication() {
  setCameraPreset(1);
  simState.currentTime = 0.0f;
  simState.timeHistory.clear();
  simState.historyIndex = -1;
  simState.rewinding = false;
}

void Application::cursorPosCallback(GLFWwindow* win, double xpos, double ypos) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onMouseMove(xpos, ypos);
}

void Application::mouseButtonCallback(GLFWwindow* win, int button, int action,
                                      int mods) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onMouseButton(button, action, mods);
}

void Application::scrollCallback(GLFWwindow* win, double xoffset,
                                 double yoffset) {
  auto* const app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(win));
  app->input.onScroll(xoffset, yoffset);
}

void Application::createDefaultPipelineConfig(
    PipelineConfigInfo& configInfo) const {
  RenderUtils::createInputAssemblyState(configInfo.inputAssembly,
                                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  configInfo.viewportState = {};
  configInfo.viewportState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  configInfo.viewportState.viewportCount = 1;
  configInfo.viewportState.scissorCount = 1;

  configInfo.rasterizer = {};
  configInfo.rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  configInfo.rasterizer.depthClampEnable = VK_FALSE;
  configInfo.rasterizer.rasterizerDiscardEnable = VK_FALSE;
  configInfo.rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  configInfo.rasterizer.lineWidth = 1.0f;
  configInfo.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  configInfo.rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  configInfo.rasterizer.depthBiasEnable = VK_FALSE;

  configInfo.multisampling = {};
  configInfo.multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  configInfo.multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  configInfo.multisampling.minSampleShading = 1.0f;

  configInfo.depthStencil = {};
  configInfo.depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  configInfo.depthStencil.depthTestEnable = VK_TRUE;
  configInfo.depthStencil.depthWriteEnable = VK_TRUE;
  configInfo.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

  RenderUtils::createColorBlendAttachment(configInfo.colorBlendAttachment);

  configInfo.colorBlending = {};
  configInfo.colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  configInfo.colorBlending.logicOpEnable = VK_FALSE;
  configInfo.colorBlending.attachmentCount = 1;
  configInfo.colorBlending.pAttachments = &configInfo.colorBlendAttachment;

  configInfo.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR};
  configInfo.dynamicState = {};
  configInfo.dynamicState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  configInfo.dynamicState.dynamicStateCount =
      static_cast<uint32_t>(configInfo.dynamicStates.size());
  configInfo.dynamicState.pDynamicStates = configInfo.dynamicStates.data();
}

void Application::getShaderStages(
    const std::string& vertPath, const std::string& fragPath,
    VkShaderModule& vertModule, VkShaderModule& fragModule,
    std::array<VkPipelineShaderStageCreateInfo, 2>& stages) const {
  std::vector<char> vertShaderCode;
  RenderUtils::readFile(vertPath, vertShaderCode);
  std::vector<char> fragShaderCode;
  RenderUtils::readFile(fragPath, fragShaderCode);
  vertModule = RenderUtils::createShaderModule(device, vertShaderCode);
  fragModule = RenderUtils::createShaderModule(device, fragShaderCode);

  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = RenderUtils::ENTRY_POINT_MAIN;

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragModule;
  fragStage.pName = RenderUtils::ENTRY_POINT_MAIN;

  stages = {vertStage, fragStage};
}

void Application::setupRenderingCreateInfo(
    VkPipelineRenderingCreateInfo& createInfo) const {
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.colorAttachmentCount = 1;
  createInfo.pColorAttachmentFormats = &swapChainImageFormat;
  createInfo.depthAttachmentFormat = depthFormat;
  createInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
  createInfo.viewMask = 0;
}

void Application::createShadowPipeline() {
  VkShaderModule vertShaderModule = VK_NULL_HANDLE;
  VkShaderModule fragShaderModule = VK_NULL_HANDLE;
  std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
  getShaderStages("shaders/shadow_vert.spv", "shaders/shadow_frag.spv",
                  vertShaderModule, fragShaderModule, shaderStages);
  PipelineConfigInfo configInfo{};
  createDefaultPipelineConfig(configInfo);
  configInfo.rasterizer.depthBiasEnable = VK_TRUE;
  configInfo.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  configInfo.colorBlending.attachmentCount = 0;
  configInfo.dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
  configInfo.dynamicState.dynamicStateCount =
      static_cast<uint32_t>(configInfo.dynamicStates.size());
  configInfo.dynamicState.pDynamicStates = configInfo.dynamicStates.data();
  const auto bindingDescription = Vertex::getBindingDescription();
  const auto attributeDescriptions = Vertex::getAttributeDescriptions();

  configInfo.vertexInputInfo = {};
  configInfo.vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  configInfo.vertexInputInfo.vertexBindingDescriptionCount = 1;
  configInfo.vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  configInfo.vertexInputInfo.vertexAttributeDescriptionCount = 1;
  configInfo.vertexInputInfo.pVertexAttributeDescriptions =
      &attributeDescriptions[0];

  struct ShadowPushConstants {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
  };
  VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(ShadowPushConstants)};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &shadowPipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("Failed to create shadow pipeline layout!");
  const VkFormat shadowDepthFormat = VK_FORMAT_D32_SFLOAT;

  VkPipelineRenderingCreateInfo renderingCreateInfo{};
  renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCreateInfo.depthAttachmentFormat = shadowDepthFormat;

  RenderUtils::createGraphicsPipeline(
      device, shadowPipelineLayout, VK_NULL_HANDLE, 2, shaderStages.data(),
      &configInfo.vertexInputInfo, &configInfo.inputAssembly,
      &configInfo.viewportState, &configInfo.rasterizer,
      &configInfo.multisampling, &configInfo.depthStencil,
      &configInfo.colorBlending, &configInfo.dynamicState, &shadowPipeline,
      &renderingCreateInfo);
  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void Application::setupViewportScissor(VkCommandBuffer commandBuffer,
                                       float width, float height) const {
  VkViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  VkRect2D scissor{
      {0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void Application::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                      uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    throw std::runtime_error("Failed to begin recording command buffer!");

  std::vector<ShadowMapData> shadowMaps;
  lightManager->getShadowSystem()->getShadowMaps(shadowMaps);

  for (size_t smIdx = 0; smIdx < shadowMaps.size(); smIdx++) {
    const auto& shadowMap = shadowMaps[smIdx];

    VkImageMemoryBarrier2 toWriteBarrier{};
    toWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toWriteBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toWriteBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    toWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toWriteBarrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toWriteBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toWriteBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toWriteBarrier.image = shadowMap.image;
    toWriteBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo toWriteDep{};
    toWriteDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toWriteDep.imageMemoryBarrierCount = 1;
    toWriteDep.pImageMemoryBarriers = &toWriteBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &toWriteDep);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = shadowMap.imageView;
    depthAttachment.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    const int shadowMapSize = 16384;
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0},
                                {static_cast<uint32_t>(shadowMapSize),
                                 static_cast<uint32_t>(shadowMapSize)}};
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      shadowPipeline);
    setupViewportScissor(commandBuffer, static_cast<float>(shadowMapSize),
                         static_cast<float>(shadowMapSize));
    vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);

    struct ShadowPushConstants {
      glm::mat4 lightSpaceMatrix;
      glm::mat4 model;
    } shadowPush;
    shadowPush.lightSpaceMatrix = shadowMap.lightSpaceMatrix;

    for (const auto& entity : registry->getEntities()) {
      const auto* renderComp = registry->getComponent<RenderComponent>(entity);
      const auto* meshComp = registry->getComponent<MeshComponent>(entity);
      const auto* transformComp = registry->getComponent<TransformComponent>(entity);
      if (!renderComp || !meshComp || !transformComp) continue;
      if (!renderComp->visible || meshComp->meshID == INVALID_MESH_ID)
        continue;
      shadowPush.model = transformComp->getModelMatrix();
      vkCmdPushConstants(commandBuffer, shadowPipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(ShadowPushConstants), &shadowPush);
      const Mesh* const mesh = meshManager->getMesh(meshComp->meshID);
      if (!mesh || mesh->getVertexBuffer() == VK_NULL_HANDLE) continue;
      VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
      vkCmdBindIndexBuffer(commandBuffer, mesh->getIndexBuffer(), 0,
                           VK_INDEX_TYPE_UINT16);
      std::vector<uint16_t> indices;
      mesh->getIndices(indices);
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1,
                       0, 0, 0);
    }
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toReadBarrier{};
    toReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toReadBarrier.srcAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toReadBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toReadBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    toReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.image = shadowMap.image;
    toReadBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo toReadDep{};
    toReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toReadDep.imageMemoryBarrierCount = 1;
    toReadDep.pImageMemoryBarriers = &toReadBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &toReadDep);
  }

  postProcessing->beginOffscreenPass(commandBuffer, swapChainExtent,
                                     sceneSettings.clearColor);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mainPipeline->getPipeline());
  setupViewportScissor(commandBuffer, static_cast<float>(swapChainExtent.width),
                       static_cast<float>(swapChainExtent.height));
  auto entities = registry->getEntities();
  for (size_t i = 0; i < entities.size(); ++i) {
    Entity entity = entities[i];
    const auto* renderComp = registry->getComponent<RenderComponent>(entity);
    const auto* meshComp = registry->getComponent<MeshComponent>(entity);
    const auto* materialComp = registry->getComponent<MaterialComponent>(entity);
    const auto* transformComp = registry->getComponent<TransformComponent>(entity);
    if (!renderComp || !meshComp || !materialComp || !transformComp) continue;
    if (!renderComp->visible || meshComp->meshID == INVALID_MESH_ID ||
        materialComp->materialID == INVALID_MATERIAL_ID)
      continue;
    const Mesh* const mesh = meshManager->getMesh(meshComp->meshID);
    const Material* const material =
        materialManager->getMaterial(materialComp->materialID);
    if (!mesh || mesh->getVertexBuffer() == VK_NULL_HANDLE || !material ||
        material->getDescriptorSet() == VK_NULL_HANDLE)
      continue;
    StandardPushConstants pushConstants;
    pushConstants.model = transformComp->getModelMatrix();
    pushConstants.layerMask = renderComp->layerMask;
    pushConstants.cameraLayer = 0xFFFFFFFF;
    pushConstants.highlightIntensity = 0.0f;
    if (interface) {
      if (entity == interface->getSelectedEntity())
        pushConstants.highlightIntensity = 0.25f;
      else if (entity == interface->getHoveredEntity())
        pushConstants.highlightIntensity = 0.12f;
    }
    vkCmdPushConstants(
        commandBuffer, mainPipeline->getPipelineLayout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(StandardPushConstants), &pushConstants);
    VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, mesh->getIndexBuffer(), 0,
                         VK_INDEX_TYPE_UINT16);
    VkDescriptorSet descriptorSetsToBind[] = {
        descriptorSets[currentFrame], material->getDescriptorSet(),
        lightManager->getShadowDescriptorSet()};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            mainPipeline->getPipelineLayout(), 0, 3,
                            descriptorSetsToBind, 0, nullptr);
    std::vector<uint16_t> indices;
    mesh->getIndices(indices);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0,
                     0, 0);
  }

  // Render sphere gizmos for point lights
  if (interface && gizmoMeshID != INVALID_MESH_ID) {
    const Mesh* gizmoMesh = meshManager->getMesh(gizmoMeshID);
    const Material* gizmoMat = materialManager->getMaterial(gizmoMaterialID);
    if (gizmoMesh && gizmoMesh->getVertexBuffer() != VK_NULL_HANDLE &&
        gizmoMat && gizmoMat->getDescriptorSet() != VK_NULL_HANDLE) {
      // Collect which entities need a gizmo
      Entity selEntity = interface->getSelectedEntity();
      Entity hovEntity = interface->getHoveredEntity();
      bool showAll = interface->getShowLightGizmos();

      auto drawGizmo = [&](Entity e, float highlight) {
        const auto* tc = registry->getComponent<TransformComponent>(e);
        if (!tc) return;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), tc->position);
        StandardPushConstants gizmoPush;
        gizmoPush.model = model;
        gizmoPush.layerMask = 0xFFFFFFFF;
        gizmoPush.cameraLayer = 0xFFFFFFFF;
        gizmoPush.highlightIntensity = highlight;
        vkCmdPushConstants(
            commandBuffer, mainPipeline->getPipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof(StandardPushConstants), &gizmoPush);
        VkBuffer vb[] = {gizmoMesh->getVertexBuffer()};
        VkDeviceSize off[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, off);
        vkCmdBindIndexBuffer(commandBuffer, gizmoMesh->getIndexBuffer(), 0,
                             VK_INDEX_TYPE_UINT16);
        VkDescriptorSet ds[] = {descriptorSets[currentFrame],
                                gizmoMat->getDescriptorSet(),
                                lightManager->getShadowDescriptorSet()};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mainPipeline->getPipelineLayout(), 0, 3, ds, 0,
                                nullptr);
        std::vector<uint16_t> idx;
        gizmoMesh->getIndices(idx);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(idx.size()), 1, 0,
                         0, 0);
      };

      if (showAll) {
        // Draw a gizmo for every point light
        for (Entity e : entities) {
          if (!registry->hasComponent<LightComponent>(e)) continue;
          const auto* lc = registry->getComponent<LightComponent>(e);
          if (!lc || lc->type == LightType::Sun) continue;
          float hl = (e == selEntity) ? 0.4f : (e == hovEntity) ? 0.25f : 0.15f;
          drawGizmo(e, hl);
        }
      } else {
        // Draw gizmo only for selected or hovered point light
        Entity gizmoEntity = selEntity;
        if (gizmoEntity == INVALID_ENTITY) gizmoEntity = hovEntity;
        if (gizmoEntity != INVALID_ENTITY &&
            registry->hasComponent<LightComponent>(gizmoEntity)) {
          const auto* lc = registry->getComponent<LightComponent>(gizmoEntity);
          if (lc && lc->type != LightType::Sun) {
            drawGizmo(gizmoEntity, 0.4f);
          }
        }
      }
    }
  }

  postProcessing->endOffscreenPass(commandBuffer);

  VkImageMemoryBarrier2 swapchainBarrier{};
  swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  swapchainBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
  swapchainBarrier.srcAccessMask = 0;
  swapchainBarrier.dstStageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  swapchainBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapchainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapchainBarrier.image = swapChainImages[imageIndex];
  swapchainBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkDependencyInfo dependencyInfo{};
  dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependencyInfo.imageMemoryBarrierCount = 1;
  dependencyInfo.pImageMemoryBarriers = &swapchainBarrier;
  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  postProcessing->render(commandBuffer, swapChainImageViews[imageIndex],
                         swapChainExtent, currentFrame);

  interface->draw(commandBuffer, imageIndex);

  swapchainBarrier.srcStageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  swapchainBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  swapchainBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  swapchainBarrier.dstAccessMask = 0;
  swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    throw std::runtime_error("Failed to record command buffer!");
}