#include "Interface.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../Rendering/MainPipeline.h"
#include "../Rendering/PostProcessing.h"
#include "WorldParser.h"

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
    case UIScalePreset::Small:  scale = 1.0f;  break;
    case UIScalePreset::Normal: scale = 1.4f;  break;
    case UIScalePreset::Large:  scale = 1.8f;  break;
    case UIScalePreset::XL:     scale = 2.2f;  break;
  }

  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark(&style);

  style.WindowPadding     = ImVec2(12, 12);
  style.FramePadding      = ImVec2(8, 5);
  style.ItemSpacing       = ImVec2(10, 8);
  style.ItemInnerSpacing  = ImVec2(8, 6);
  style.IndentSpacing     = 22.0f;
  style.ScrollbarSize     = 16.0f;
  style.GrabMinSize       = 12.0f;
  style.WindowRounding    = 6.0f;
  style.FrameRounding     = 4.0f;
  style.PopupRounding     = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.GrabRounding      = 3.0f;
  style.TabRounding       = 4.0f;
  style.SeparatorTextPadding = ImVec2(20, 4);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_WindowBg]        = ImVec4(0.10f, 0.10f, 0.13f, 0.97f);
  colors[ImGuiCol_PopupBg]         = ImVec4(0.10f, 0.10f, 0.14f, 0.97f);
  colors[ImGuiCol_Border]          = ImVec4(0.28f, 0.28f, 0.35f, 0.60f);
  colors[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.16f, 0.21f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);
  colors[ImGuiCol_FrameBgActive]   = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
  colors[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
  colors[ImGuiCol_TitleBgActive]   = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
  colors[ImGuiCol_MenuBarBg]       = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
  colors[ImGuiCol_Header]          = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
  colors[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.32f, 0.45f, 1.00f);
  colors[ImGuiCol_HeaderActive]    = ImVec4(0.35f, 0.38f, 0.55f, 1.00f);
  colors[ImGuiCol_Button]          = ImVec4(0.20f, 0.22f, 0.30f, 1.00f);
  colors[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.34f, 0.46f, 1.00f);
  colors[ImGuiCol_ButtonActive]    = ImVec4(0.35f, 0.40f, 0.55f, 1.00f);
  colors[ImGuiCol_SliderGrab]      = ImVec4(0.40f, 0.50f, 0.75f, 1.00f);
  colors[ImGuiCol_SliderGrabActive]= ImVec4(0.50f, 0.60f, 0.85f, 1.00f);
  colors[ImGuiCol_CheckMark]       = ImVec4(0.50f, 0.65f, 1.00f, 1.00f);
  colors[ImGuiCol_Separator]       = ImVec4(0.28f, 0.28f, 0.35f, 0.50f);
  colors[ImGuiCol_Tab]             = ImVec4(0.16f, 0.16f, 0.21f, 1.00f);
  colors[ImGuiCol_TabHovered]      = ImVec4(0.30f, 0.34f, 0.46f, 1.00f);

  style.ScaleAllSizes(scale);
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
                       const Registry& registry, MainPipeline* mainPipeline,
                       PostProcessing* postProcessing) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (ImGui::BeginMainMenuBar()) {
    auto menuItem = [](const char* label) -> bool {
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14, 8));
      bool open = ImGui::BeginMenu(label);
      ImGui::PopStyleVar();
      return open;
    };

    if (menuItem("Worlds")) {
      renderWorldsMenu();
      ImGui::EndMenu();
    }
    if (menuItem("Simulation")) {
      renderSimulationMenu(simState);
      ImGui::EndMenu();
    }
    if (menuItem("Objects")) {
      renderObjectsMenu(registry);
      ImGui::EndMenu();
    }
    if (menuItem("Scene")) {
      renderSceneMenu(sceneSettings, mainPipeline);
      ImGui::EndMenu();
    }
    if (menuItem("Post Processing")) {
      renderPostProcessingMenu(postProcessing);
      ImGui::EndMenu();
    }
    if (menuItem("Settings")) {
      renderSettingsMenu();
      ImGui::EndMenu();
    }

    if (generalSettings.showFPS) {
      const float fps = ImGui::GetIO().Framerate;
      char fpsText[48];
      snprintf(fpsText, sizeof(fpsText), "%.0f FPS  |  %.1f ms", fps,
               1000.0f / fps);
      const float textWidth = ImGui::CalcTextSize(fpsText).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 20.0f);
      if (fps >= 60.0f)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", fpsText);
      else if (fps >= 30.0f)
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", fpsText);
      else
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", fpsText);
    }
    ImGui::EndMainMenuBar();
  }

  ImGui::Render();
}

static void keybadge(const char* key) {
  ImGui::SameLine();
  ImVec2 textSize = ImGui::CalcTextSize(key);
  ImVec2 padding(6.0f, 2.0f);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  pos.y += 1.0f;
  ImVec2 br(pos.x + textSize.x + padding.x * 2,
            pos.y + textSize.y + padding.y * 2);
  ImGui::GetWindowDrawList()->AddRectFilled(
      pos, br, IM_COL32(60, 65, 80, 220), 4.0f);
  ImGui::GetWindowDrawList()->AddRect(
      pos, br, IM_COL32(90, 95, 115, 180), 4.0f);
  ImGui::SetCursorScreenPos(ImVec2(pos.x + padding.x, pos.y + padding.y));
  ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1.0f), "%s", key);
}

static void sectionHeader(const char* label) {
  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.50f, 0.70f, 1.0f, 1.0f), "%s", label);
  ImGui::Separator();
  ImGui::Spacing();
}

void Interface::renderSimulationMenu(SimulationState& simState) {
  sectionHeader("Transport");

  const float bw = 90.0f;
  const float bh = 28.0f;

  if (simState.isPaused) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.22f, 0.60f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.26f, 0.70f, 0.32f, 1.0f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.75f, 0.28f, 0.28f, 1.0f));
  }
  if (ImGui::Button(simState.isPaused ? "  Play  " : "  Pause  ",
                    ImVec2(bw, bh))) {
    simState.isPaused = !simState.isPaused;
    simState.rewinding = false;
  }
  ImGui::PopStyleColor(3);
  keybadge("Space");

  ImGui::Spacing();

  if (ImGui::Button("Step Fwd", ImVec2(bw, bh))) {
    simState.isPaused = true;
    simState.stepFrame = true;
    simState.rewinding = false;
  }
  keybadge(".");
  ImGui::SameLine(0, 16);
  if (ImGui::Button("Step Back", ImVec2(bw, bh))) {
    if (!simState.timeHistory.empty()) {
      simState.isPaused = true;
      simState.rewinding = true;
      if (simState.historyIndex < 0)
        simState.historyIndex =
            static_cast<int>(simState.timeHistory.size()) - 1;
      if (simState.historyIndex > 0) {
        simState.historyIndex--;
        simState.currentTime = simState.timeHistory[simState.historyIndex];
      }
    }
  }
  keybadge(",");

  ImGui::Spacing();

  if (ImGui::Button("Restart", ImVec2(bw, bh))) {
    simState.currentTime = 0.0f;
    simState.timeHistory.clear();
    simState.historyIndex = -1;
    simState.rewinding = false;
  }
  keybadge("R");
  ImGui::SameLine(0, 16);
  if (ImGui::Button("Half Speed", ImVec2(bw, bh))) {
    simState.timeSpeed = glm::max(simState.timeSpeed * 0.5f, 0.01f);
  }
  keybadge("-");
  ImGui::SameLine(0, 16);
  if (ImGui::Button("Double Spd", ImVec2(bw, bh))) {
    simState.timeSpeed = glm::min(simState.timeSpeed * 2.0f, 10.0f);
  }
  keybadge("+");

  sectionHeader("Time");

  int minutes = static_cast<int>(simState.currentTime) / 60;
  int seconds = static_cast<int>(simState.currentTime) % 60;
  int millis = static_cast<int>(
      (simState.currentTime - static_cast<int>(simState.currentTime)) * 100);
  ImGui::Text("Elapsed:  %02d:%02d.%02d", minutes, seconds, millis);

  ImGui::Spacing();

  ImGui::SetNextItemWidth(220.0f);
  ImGui::SliderFloat("Speed", &simState.timeSpeed, 0.0f, 10.0f, "%.2fx");
  ImGui::SameLine(0, 10);
  if (ImGui::SmallButton("1x##spd")) simState.timeSpeed = 1.0f;

  ImGui::SetNextItemWidth(220.0f);
  ImGui::SliderFloat("Step Size", &simState.stepSize, 0.001f, 1.0f, "%.3fs");
  ImGui::SameLine(0, 10);
  if (ImGui::SmallButton("16ms##step")) simState.stepSize = 0.016f;

  if (!simState.timeHistory.empty()) {
    sectionHeader("Timeline");

    int histSize = static_cast<int>(simState.timeHistory.size());
    int scrubIdx =
        simState.historyIndex >= 0 ? simState.historyIndex : histSize - 1;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##timeline", &scrubIdx, 0, histSize - 1,
                         "Frame %d")) {
      simState.isPaused = true;
      simState.rewinding = true;
      simState.historyIndex = scrubIdx;
      simState.currentTime = simState.timeHistory[scrubIdx];
    }
    ImGui::TextDisabled("History: %d frames (%.1fs)", histSize,
                        simState.timeHistory.back());
  }

  sectionHeader("Status");

  ImVec4 statusColor = simState.isPaused
                           ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
                           : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
  const char* statusText = simState.isPaused
                               ? (simState.rewinding ? "Rewinding" : "Paused")
                               : "Running";
  ImGui::Bullet();
  ImGui::SameLine();
  ImGui::TextColored(statusColor, "%s", statusText);
  ImGui::SameLine(0, 16);
  ImGui::TextDisabled("%.2fx", simState.timeSpeed);
}

void Interface::renderObjectsMenu(const Registry& registry) {
  auto entities = registry.getEntities();
  int meshCount = 0;
  int lightCount = 0;
  std::vector<Entity> meshEntities;
  std::vector<Entity> lightEntities;

  for (Entity e : entities) {
    if (registry.hasComponent<LightComponent>(e)) {
      lightCount++;
      lightEntities.push_back(e);
    } else if (registry.hasComponent<MeshComponent>(e)) {
      meshCount++;
      meshEntities.push_back(e);
    }
  }

  ImGui::TextDisabled("%d entities  |  %d objects  |  %d lights",
                      static_cast<int>(entities.size()), meshCount, lightCount);
  ImGui::Separator();
  ImGui::Spacing();

  const float labelW = 110.0f;

  auto propRow = [&](const char* label, const char* fmt, ...) {
    ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "%s", label);
    ImGui::SameLine(labelW);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
  };

  if (ImGui::TreeNodeEx("Objects",
                        ImGuiTreeNodeFlags_DefaultOpen |
                            ImGuiTreeNodeFlags_SpanAvailWidth)) {
    for (Entity e : meshEntities) {
      const auto* nameComp = registry.getComponent<NameComponent>(e);
      std::string name = nameComp ? nameComp->name : "Unknown";

      ImGui::PushID(static_cast<int>(e));
      if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        ImGui::Spacing();
        const auto* transform = registry.getComponent<TransformComponent>(e);
        if (transform) {
          propRow("Position", "%.2f, %.2f, %.2f", transform->position.x,
                  transform->position.y, transform->position.z);
          propRow("Scale", "%.2f, %.2f, %.2f", transform->scale.x,
                  transform->scale.y, transform->scale.z);
        }
        const auto* meshComp = registry.getComponent<MeshComponent>(e);
        if (meshComp) propRow("Mesh", "#%u", meshComp->meshID);
        const auto* matComp = registry.getComponent<MaterialComponent>(e);
        if (matComp) propRow("Material", "#%u", matComp->materialID);
        const auto* renderComp = registry.getComponent<RenderComponent>(e);
        if (renderComp)
          propRow("Visible", "%s", renderComp->visible ? "Yes" : "No");
        ImGui::Spacing();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::Spacing();

  if (ImGui::TreeNodeEx("Lights",
                        ImGuiTreeNodeFlags_DefaultOpen |
                            ImGuiTreeNodeFlags_SpanAvailWidth)) {
    for (Entity e : lightEntities) {
      const auto* nameComp = registry.getComponent<NameComponent>(e);
      std::string name = nameComp ? nameComp->name : "Unknown";

      ImGui::PushID(static_cast<int>(e));
      if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        ImGui::Spacing();
        const auto* light = registry.getComponent<LightComponent>(e);
        const auto* transform = registry.getComponent<TransformComponent>(e);
        if (light) {
          propRow("Type", "%s",
                  light->type == LightType::Sun ? "Sun" : "Point");
          if (transform)
            propRow("Position", "%.2f, %.2f, %.2f", transform->position.x,
                    transform->position.y, transform->position.z);
          propRow("Direction", "%.2f, %.2f, %.2f", light->direction.x,
                  light->direction.y, light->direction.z);

          ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Color");
          ImGui::SameLine(labelW);
          ImVec4 lc(light->color.r, light->color.g, light->color.b, 1.0f);
          ImGui::ColorButton("##lc", lc,
                             ImGuiColorEditFlags_NoBorder |
                                 ImGuiColorEditFlags_NoTooltip,
                             ImVec2(16, 16));
          ImGui::SameLine(0, 6);
          ImGui::Text("%.2f, %.2f, %.2f", light->color.r, light->color.g,
                      light->color.b);

          propRow("Intensity", "%.2f", light->intensity);
          propRow("Shadows", "%s", light->castsShadows ? "Yes" : "No");
        }
        ImGui::Spacing();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
}

void Interface::renderSceneMenu(SceneSettings& sceneSettings,
                                MainPipeline* mainPipeline) {
  sectionHeader("Background");

  ImGui::ColorPicker3("##clearcolor", &sceneSettings.clearColor[0],
                      ImGuiColorEditFlags_PickerHueWheel |
                          ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_NoInputs);
  ImGui::Spacing();
  ImGui::ColorEdit3("Clear Color", &sceneSettings.clearColor[0],
                    ImGuiColorEditFlags_NoLabel);

  sectionHeader("Shading");

  const char* shadingItems[] = {"Phong", "Gouraud"};
  int currentShading =
      (mainPipeline->getShadingMode() == MainPipeline::ShadingMode::Phong) ? 0
                                                                           : 1;
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::Combo("Shading Mode", &currentShading, shadingItems,
                   IM_ARRAYSIZE(shadingItems))) {
    mainPipeline->setShadingMode(currentShading == 0
                                     ? MainPipeline::ShadingMode::Phong
                                     : MainPipeline::ShadingMode::Gouraud);
    mainPipeline->recreate();
  }
  keybadge("L");

  ImGui::Spacing();

  const char* polygonItems[] = {"Fill", "Line", "Point"};
  static int currentPolygon = 0;
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::Combo("Render Mode", &currentPolygon, polygonItems,
                   IM_ARRAYSIZE(polygonItems))) {
    VkPolygonMode mode = VK_POLYGON_MODE_FILL;
    if (currentPolygon == 1) mode = VK_POLYGON_MODE_LINE;
    if (currentPolygon == 2) mode = VK_POLYGON_MODE_POINT;
    mainPipeline->setPolygonMode(mode);
    mainPipeline->recreate();
  }
  keybadge("Tab");
}

void Interface::renderPostProcessingMenu(PostProcessing* postProcessing) {
  PostProcessingConfig config = postProcessing->getConfig();
  bool changed = false;

  sectionHeader("Color Grading");

  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("Hue", &config.hue, -1.0f, 1.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10);
  if (ImGui::SmallButton("Reset##hue")) {
    config.hue = 0.0f;
    changed = true;
  }

  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("Saturation", &config.saturation, 0.0f, 2.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10);
  if (ImGui::SmallButton("Reset##sat")) {
    config.saturation = 1.0f;
    changed = true;
  }

  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("Contrast", &config.contrast, 0.0f, 2.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10);
  if (ImGui::SmallButton("Reset##con")) {
    config.contrast = 1.0f;
    changed = true;
  }

  sectionHeader("Effects");

  if (ImGui::Checkbox("Toon Shader", &config.useToon)) {
    postProcessing->setToonMode(config.useToon);
    changed = true;
  }
  keybadge("K");

  ImGui::Spacing();
  ImGui::Spacing();

  if (ImGui::Button("Reset All", ImVec2(120, 28))) {
    config.hue = 0.0f;
    config.saturation = 1.0f;
    config.contrast = 1.0f;
    config.useToon = false;
    postProcessing->setToonMode(false);
    changed = true;
  }

  if (changed) {
    postProcessing->setConfig(config);
  }
}

void Interface::renderSettingsMenu() {
  sectionHeader("Interface");

  const char* presetItems[] = {"Small", "Normal", "Large", "XL"};
  int currentPreset = static_cast<int>(generalSettings.scalePreset);
  ImGui::SetNextItemWidth(140.0f);
  if (ImGui::Combo("UI Scale", &currentPreset, presetItems,
                   IM_ARRAYSIZE(presetItems))) {
    generalSettings.scalePreset = static_cast<UIScalePreset>(currentPreset);
    applyScalePreset();
  }

  ImGui::Checkbox("Show FPS", &generalSettings.showFPS);
  keybadge("F1");

  sectionHeader("Keyboard Shortcuts");

  auto shortcutRow = [](const char* key, const char* desc) {
    ImVec2 textSize = ImGui::CalcTextSize(key);
    ImVec2 pad(6.0f, 2.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 br(pos.x + textSize.x + pad.x * 2, pos.y + textSize.y + pad.y * 2);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, br,
                                              IM_COL32(60, 65, 80, 220), 4.0f);
    ImGui::GetWindowDrawList()->AddRect(pos, br, IM_COL32(90, 95, 115, 180),
                                        4.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad.x, pos.y + pad.y));
    ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1.0f), "%s", key);
    ImGui::SameLine(150);
    ImGui::Text("%s", desc);
  };

  ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "General");
  ImGui::Spacing();
  shortcutRow("ESC", "Exit application");
  shortcutRow("F1", "Toggle FPS display");
  shortcutRow("F11", "Toggle fullscreen");

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Simulation");
  ImGui::Spacing();
  shortcutRow("Space", "Pause / Resume");
  shortcutRow(".", "Step forward");
  shortcutRow(",", "Step backward / rewind");
  shortcutRow("R", "Restart simulation");
  shortcutRow("+", "Double speed");
  shortcutRow("-", "Half speed");

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Rendering");
  ImGui::Spacing();
  shortcutRow("L", "Cycle shading mode");
  shortcutRow("K", "Toggle toon shader");
  shortcutRow("Tab", "Cycle render mode");
  shortcutRow("G", "Toggle wireframe overlay");

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "Camera");
  ImGui::Spacing();
  shortcutRow("Enter", "Toggle Orbit / FPS");
  shortcutRow("W/A/S/D", "Move (FPS mode)");
  shortcutRow("Space", "Camera up (FPS)");
  shortcutRow("Shift", "Camera down (FPS)");
  shortcutRow("RMB", "Orbit drag");
  shortcutRow("Scroll", "Zoom / camera speed");
  shortcutRow("Ctrl+Arrows", "Pan camera");
  shortcutRow("1", "Camera preset 1");
  shortcutRow("2", "Camera preset 2");
}

void Interface::setWorldLoadCallback(
    std::function<void(const std::string&)> callback) {
  worldLoadCallback = std::move(callback);
}

void Interface::setWorldDirectory(const std::string& dir) {
  worldDirectory = dir;
  refreshWorldList();
}

void Interface::refreshWorldList() {
  worldFiles = WorldParser::listWorlds(worldDirectory);
  worldFileStats.clear();
  for (const auto& path : worldFiles) {
    WorldFileStats stats{};
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.find("BeginObject") != std::string::npos) stats.objects++;
      if (line.find("BeginLight") != std::string::npos) stats.lights++;
      if (line.find("BeginTexture") != std::string::npos) stats.textures++;
      if (line.find("BeginMaterial") != std::string::npos) stats.materials++;
    }
    worldFileStats[path] = stats;
  }
}

void Interface::renderWorldsMenu() {
  if (worldFiles.empty()) {
    ImGui::TextDisabled("No .world files found in '%s'",
                        worldDirectory.c_str());
  } else {
    for (const auto& path : worldFiles) {
      std::string displayName = std::filesystem::path(path).stem().string();

      ImGui::PushID(path.c_str());
      if (ImGui::Selectable(displayName.c_str(), false, 0, ImVec2(0, 0))) {
        if (worldLoadCallback) {
          worldLoadCallback(path);
        }
      }
      ImGui::SameLine(200);
      auto it = worldFileStats.find(path);
      if (it != worldFileStats.end()) {
        const auto& s = it->second;
        ImGui::TextDisabled("%d obj  %d lit  %d tex  %d mat", s.objects,
                            s.lights, s.textures, s.materials);
      }
      ImGui::PopID();
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Refresh", ImVec2(100, 28))) {
    refreshWorldList();
  }
  ImGui::SameLine(0, 12);
  ImGui::TextDisabled("%d world(s)", static_cast<int>(worldFiles.size()));
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