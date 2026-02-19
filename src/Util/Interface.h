#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "../Resources/Object.h"

class MainPipeline;
class PostProcessing;

struct SimulationState {
  bool isPaused = false;
  bool stepFrame = false;
  float timeSpeed = 1.0f;
  float currentTime = 0.0f;
};

struct SceneSettings {
  glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};

enum class UIScalePreset { Small, Normal, Large, XL };

struct GeneralSettings {
  UIScalePreset scalePreset = UIScalePreset::Normal;
  bool showFPS = true;
};

class Interface {
 public:
  Interface(GLFWwindow* window, VkInstance instance,
            VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
            VkCommandPool commandPool, uint32_t queueFamily,
            VkFormat swapChainFormat, VkFormat depthFormat);
  ~Interface();

  void init();
  void resize(VkExtent2D extent, const std::vector<VkImageView>& imageViews);
  void cleanup();

  void render(SimulationState& simState, SceneSettings& sceneSettings,
              const std::vector<Object>& objects, MainPipeline* mainPipeline,
              PostProcessing* postProcessing);

  void draw(VkCommandBuffer commandBuffer, uint32_t imageIndex);

 private:
  GLFWwindow* window;
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkQueue queue;
  VkCommandPool commandPool;
  uint32_t queueFamily;
  VkFormat swapChainFormat;
  VkFormat depthFormat;
  VkDescriptorPool descriptorPool;

  VkRenderPass imGuiRenderPass;
  std::vector<VkFramebuffer> framebuffers;
  VkExtent2D currentExtent;

  GeneralSettings generalSettings;

  void createDescriptorPool();
  void createImGuiRenderPass();
  void applyScalePreset();
  void renderSimulationMenu(SimulationState& simState);
  void renderObjectsMenu(const std::vector<Object>& objects);
  void renderSceneMenu(SceneSettings& sceneSettings,
                       MainPipeline* mainPipeline);
  void renderPostProcessingMenu(PostProcessing* postProcessing);
  void renderSettingsMenu();
};