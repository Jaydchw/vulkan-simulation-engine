#pragma once
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ECS/Entity.h"
#include "../ECS/Registry.h"

class MainPipeline;
class PostProcessing;

struct SimulationState {
  bool isPaused = false;
  bool stepFrame = false;
  float timeSpeed = 1.0f;
  float currentTime = 0.0f;
  float stepSize = 0.016f;
  std::vector<float> timeHistory;
  int historyIndex = -1;
  bool rewinding = false;
  bool reversePlay = false;
  bool snapshotScrubbed = false;
  bool resetRequested = false;
};

struct SceneSettings {
  glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};

enum class UIScalePreset { Small, Normal, Large, XL };

struct GeneralSettings {
  UIScalePreset scalePreset = UIScalePreset::Normal;
  bool showFPS = true;
  bool showLightGizmos = false;
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
              Registry& registry, MainPipeline* mainPipeline,
              PostProcessing* postProcessing);

  void draw(VkCommandBuffer commandBuffer, uint32_t imageIndex);

  Entity getSelectedEntity() const { return selectedEntity; }
  Entity getHoveredEntity() const { return hoveredEntity; }
  bool getShowLightGizmos() const { return generalSettings.showLightGizmos; }
  void clearSelection() { selectedEntity = INVALID_ENTITY; hoveredEntity = INVALID_ENTITY; }

  void setWorldLoadCallback(std::function<void(const std::string&)> callback);
  void setWorldDirectory(const std::string& dir);
  void refreshWorldList();

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
  float currentScale = 1.0f;

  Entity selectedEntity = INVALID_ENTITY;
  Entity hoveredEntity = INVALID_ENTITY;

  std::string worldDirectory;
  std::vector<std::string> worldFiles;
  std::function<void(const std::string&)> worldLoadCallback;

  struct WorldFileStats {
    int objects = 0;
    int lights = 0;
    int textures = 0;
    int materials = 0;
  };
  std::unordered_map<std::string, WorldFileStats> worldFileStats;

  void createDescriptorPool();
  void createImGuiRenderPass();
  void applyScalePreset();
  void renderWorldsMenu();
  void renderSimulationMenu(SimulationState& simState);
  void renderObjectsMenu(Registry& registry);
  void renderSceneMenu(SceneSettings& sceneSettings,
                       MainPipeline* mainPipeline);
  void renderPostProcessingMenu(PostProcessing* postProcessing);
  void renderSettingsMenu();
};