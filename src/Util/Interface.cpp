#include "Interface.h"

#include <vulkan/vulkan.h>

#include <stdexcept>

#include "../Rendering/MainPipeline.h"
#include "../Rendering/PostProcessing.h"

Interface::Interface(GLFWwindow* win, VkInstance inst, VkPhysicalDevice physDev,
                     VkDevice dev, VkQueue q, VkCommandPool cmdPool,
                     uint32_t qFam, VkFormat scFmt, VkFormat dFmt)
    : window(win),
      instance(inst),
      physicalDevice(physDev),
      device(dev),
      queue(q),
      commandPool(cmdPool),
      queueFamily(qFam),
      swapChainFormat(scFmt),
      depthFormat(dFmt),
      descriptorPool(VK_NULL_HANDLE),
      imGuiRenderPass(VK_NULL_HANDLE),
      currentExtent{0, 0} {}

Interface::~Interface() { cleanup(); }

void Interface::init() {
  createDescriptorPool();
  createImGuiRenderPass();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForVulkan(window, true);

  ImGui_ImplVulkan_InitInfo initInfo = {};
  initInfo.ApiVersion = VK_API_VERSION_1_3;
  initInfo.Instance = instance;
  initInfo.PhysicalDevice = physicalDevice;
  initInfo.Device = device;
  initInfo.QueueFamily = queueFamily;
  initInfo.Queue = queue;
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = descriptorPool;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = 2;
  initInfo.PipelineInfoMain.RenderPass = imGuiRenderPass;
  initInfo.PipelineInfoMain.Subpass = 0;
  initInfo.Allocator = nullptr;
  initInfo.CheckVkResultFn = nullptr;

  ImGui_ImplVulkan_Init(&initInfo);
  applyScalePreset();
}

void Interface::applyScalePreset() {
  float scale = 1.0f;
  switch (generalSettings.scalePreset) {
    case UIScalePreset::Small:  scale = 0.8f;  break;
    case UIScalePreset::Normal: scale = 1.0f;  break;
    case UIScalePreset::Large:  scale = 1.4f;  break;
    case UIScalePreset::XL:     scale = 1.8f;  break;
  }
  ImGuiStyle style;
  ImGui::StyleColorsDark(&style);
  style.ScaleAllSizes(scale);
  ImGui::GetStyle() = style;
  ImGui::GetIO().FontGlobalScale = scale;
}

void Interface::resize(VkExtent2D extent,
                       const std::vector<VkImageView>& imageViews) {
  currentExtent = extent;

  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  framebuffers.clear();

  framebuffers.resize(imageViews.size());

  for (size_t i = 0; i < imageViews.size(); i++) {
    VkImageView attachments[] = {imageViews[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = imGuiRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                            &framebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create ImGui framebuffer!");
    }
  }
}

void Interface::cleanup() {
  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  framebuffers.clear();

  if (imGuiRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, imGuiRenderPass, nullptr);
    imGuiRenderPass = VK_NULL_HANDLE;
  }

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    descriptorPool = VK_NULL_HANDLE;
  }
}

void Interface::createDescriptorPool() {
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
  pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui descriptor pool");
  }
}

void Interface::createImGuiRenderPass() {
  VkAttachmentDescription attachment = {};
  attachment.format = swapChainFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_attachment = {};
  color_attachment.attachment = 0;
  color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &info, nullptr, &imGuiRenderPass) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui RenderPass");
  }
}

void Interface::render(SimulationState& simState, SceneSettings& sceneSettings,
const Registry& registry,
MainPipeline* mainPipeline,
PostProcessing* postProcessing) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Simulation")) {
      renderSimulationMenu(simState);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Objects")) {
      renderObjectsMenu(registry);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scene")) {
      renderSceneMenu(sceneSettings, mainPipeline);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Post Processing")) {
      renderPostProcessingMenu(postProcessing);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
      renderSettingsMenu();
      ImGui::EndMenu();
    }
    if (generalSettings.showFPS) {
      ImGui::SameLine(ImGui::GetWindowWidth() - 100);
      ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);
    }
    ImGui::EndMainMenuBar();
  }

  ImGui::Render();
}

void Interface::renderSimulationMenu(SimulationState& simState) {
  if (ImGui::Button(simState.isPaused ? "Resume" : "Pause")) {
    simState.isPaused = !simState.isPaused;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step")) {
    simState.isPaused = true;
    simState.stepFrame = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Restart")) {
    simState.currentTime = 0.0f;
  }
  ImGui::SliderFloat("Time Speed", &simState.timeSpeed, 0.0f, 5.0f);
  ImGui::Text("Time: %.2f", simState.currentTime);
}

void Interface::renderObjectsMenu(const Registry& registry) {
  auto entities = registry.getEntities();
  for (size_t i = 0; i < entities.size(); ++i) {
    Entity entity = entities[i];
    const auto* nameComp = registry.getComponent<NameComponent>(entity);
    std::string name = nameComp ? nameComp->name : "Unknown";
    ImGui::Text("%s", name.c_str());

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("Entity: %u", entity);
      const auto* transform = registry.getComponent<TransformComponent>(entity);
      if (transform) {
        ImGui::Text("Pos: %.2f, %.2f, %.2f", transform->position.x,
                    transform->position.y, transform->position.z);
      }
      const auto* meshComp = registry.getComponent<MeshComponent>(entity);
      if (meshComp) {
        ImGui::Text("Mesh ID: %u", meshComp->meshID);
      }
      const auto* materialComp = registry.getComponent<MaterialComponent>(entity);
      if (materialComp) {
        ImGui::Text("Material ID: %u", materialComp->materialID);
      }
      ImGui::EndTooltip();
    }
  }
}

void Interface::renderSceneMenu(SceneSettings& sceneSettings,
                                MainPipeline* mainPipeline) {
  ImGui::ColorEdit3("Background Color", &sceneSettings.clearColor[0]);

  const char* shadingItems[] = {"Phong", "Gouraud"};
  int currentShading =
      (mainPipeline->getShadingMode() == MainPipeline::ShadingMode::Phong) ? 0
                                                                           : 1;
  if (ImGui::Combo("Shading Mode", &currentShading, shadingItems,
                   IM_ARRAYSIZE(shadingItems))) {
    mainPipeline->setShadingMode(currentShading == 0
                                     ? MainPipeline::ShadingMode::Phong
                                     : MainPipeline::ShadingMode::Gouraud);
    mainPipeline->recreate();
  }

  const char* polygonItems[] = {"Fill", "Line", "Point"};
  static int currentPolygon = 0;
  if (ImGui::Combo("Rendering Mode", &currentPolygon, polygonItems,
                   IM_ARRAYSIZE(polygonItems))) {
    VkPolygonMode mode = VK_POLYGON_MODE_FILL;
    if (currentPolygon == 1) mode = VK_POLYGON_MODE_LINE;
    if (currentPolygon == 2) mode = VK_POLYGON_MODE_POINT;
    mainPipeline->setPolygonMode(mode);
    mainPipeline->recreate();
  }
}

void Interface::renderPostProcessingMenu(PostProcessing* postProcessing) {
  PostProcessingConfig config = postProcessing->getConfig();
  bool changed = false;

  if (ImGui::SliderFloat("Hue", &config.hue, -1.0f, 1.0f)) changed = true;
  if (ImGui::SliderFloat("Saturation", &config.saturation, 0.0f, 2.0f))
    changed = true;
  if (ImGui::SliderFloat("Contrast", &config.contrast, 0.0f, 2.0f))
    changed = true;

  if (ImGui::Checkbox("Toon Shader", &config.useToon)) {
    postProcessing->setToonMode(config.useToon);
    changed = true;
  }

  if (changed) {
    postProcessing->setConfig(config);
  }
}

void Interface::renderSettingsMenu() {
  const char* presetItems[] = {"Small", "Normal", "Large", "XL"};
  int currentPreset = static_cast<int>(generalSettings.scalePreset);
  if (ImGui::Combo("UI Scale", &currentPreset, presetItems,
                   IM_ARRAYSIZE(presetItems))) {
    generalSettings.scalePreset = static_cast<UIScalePreset>(currentPreset);
    applyScalePreset();
  }

  ImGui::Checkbox("Show FPS", &generalSettings.showFPS);
}

void Interface::draw(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
  VkRenderPassBeginInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  info.renderPass = imGuiRenderPass;
  info.framebuffer = framebuffers[imageIndex];
  info.renderArea.extent = currentExtent;
  info.renderArea.offset = {0, 0};

  vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
  vkCmdEndRenderPass(commandBuffer);
}