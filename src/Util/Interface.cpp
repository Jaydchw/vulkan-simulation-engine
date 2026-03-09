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
  currentScale = scale;

  ImGuiStyle style;
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
Registry& registry, MainPipeline* mainPipeline,
PostProcessing* postProcessing) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Clear selection if entity no longer exists
  if (selectedEntity != INVALID_ENTITY &&
      !registry.hasComponent<NameComponent>(selectedEntity)) {
    selectedEntity = INVALID_ENTITY;
  }

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
    if (menuItem("Objects")) {
      renderObjectsMenu(registry);
      ImGui::EndMenu();
    } else {
      selectedEntity = INVALID_ENTITY;
      hoveredEntity = INVALID_ENTITY;
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
      renderSettingsMenu(simState);
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

  renderTransportBar(simState);

  // Auto-open stats window when a performance bake just completed
  if (simState.bakePerformanceMode && simState.baked && simState.bakeStats.hasData &&
      !simState.isBaking && simState.bakeCurrentStep == simState.bakeTotalSteps) {
    showBakeStatsWindow = true;
  }

  ImGui::Render();
}

static void keybadge(const char* key) {
  ImGui::SameLine(0, 8);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.44f, 0.52f, 1.0f));
  ImGui::Text("[%s]", key);
  ImGui::PopStyleColor();
}

static void sectionHeader(const char* label) {
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.62f, 0.88f, 1.0f));
  ImGui::Text("%s", label);
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

static void fieldLabel(const char* label) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.58f, 0.68f, 1.0f));
  ImGui::Text("%s", label);
  ImGui::PopStyleColor();
}

void Interface::renderTransportBar(SimulationState& simState) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float s = currentScale;
  const float barHeight = 38.0f * s;
  const float btnH = 24.0f * s;
  const float btnW = 24.0f * s;
  const float gap = 4.0f * s;
  const float sepGap = 10.0f * s;
  const bool snapshotsOn = generalSettings.snapshotsEnabled;

  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x,
             viewport->WorkPos.y + viewport->WorkSize.y - barHeight));
  ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, barHeight));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * s, 7 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * s, 3 * s));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.14f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.28f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.23f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.34f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.32f, 0.42f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.55f, 0.80f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.65f, 0.90f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));

  if (ImGui::Begin("##transport_bar", nullptr, flags)) {
    // --- Restart ---
    if (ImGui::Button("##restart", ImVec2(btnW, btnH))) {
      simState.resetRequested = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart [R]");
    {
      ImVec2 p = ImGui::GetItemRectMin();
      ImVec2 sz = ImGui::GetItemRectSize();
      float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
      float r = 4.0f * s;
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r),
          IM_COL32(200, 200, 200, 220), 1.0f * s);
    }
    ImGui::SameLine(0, gap);

    // --- Step Back (snapshots only) ---
    if (snapshotsOn) {
      if (ImGui::Button("##step_back", ImVec2(btnW, btnH))) {
        simState.isPaused = true;
        simState.rewinding = true;
        simState.reversePlay = false;
        simState.snapshotScrubbed = true;
        if (simState.historyIndex < 0)
          simState.historyIndex =
              static_cast<int>(simState.timeHistory.size()) - 1;
        if (simState.historyIndex > 0) {
          simState.historyIndex--;
          if (simState.historyIndex <
              static_cast<int>(simState.timeHistory.size()))
            simState.currentTime = simState.timeHistory[simState.historyIndex];
        }
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Back [,]");
      {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
        float r = 4.0f * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(cx - r, cy - r), ImVec2(cx - r + 2 * s, cy + r),
                          IM_COL32(200, 200, 200, 220));
        dl->AddTriangleFilled(ImVec2(cx + r, cy - r), ImVec2(cx + r, cy + r),
                              ImVec2(cx - r + 3 * s, cy),
                              IM_COL32(200, 200, 200, 220));
      }
      ImGui::SameLine(0, gap);

      // --- Reverse (snapshots only) ---
      {
        bool revActive = simState.reversePlay && !simState.isPaused;
        if (revActive) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.18f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.38f, 0.22f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.45f, 0.28f, 1.0f));
        }
        if (ImGui::Button("##reverse", ImVec2(btnW, btnH))) {
          if (revActive) {
            simState.isPaused = true;
            simState.reversePlay = false;
          } else {
            simState.isPaused = false;
            simState.reversePlay = true;
            simState.rewinding = true;
            simState.scrubAccumulator = 0.0f;
          }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reverse Play");
        {
          ImVec2 p = ImGui::GetItemRectMin();
          ImVec2 sz = ImGui::GetItemRectSize();
          float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
          float r = 4.0f * s;
          ImDrawList* dl = ImGui::GetWindowDrawList();
          dl->AddTriangleFilled(ImVec2(cx + r * 0.4f, cy - r), ImVec2(cx + r * 0.4f, cy + r),
                                ImVec2(cx - r, cy), IM_COL32(200, 200, 200, 220));
          dl->AddTriangleFilled(ImVec2(cx + r, cy - r), ImVec2(cx + r, cy + r),
                                ImVec2(cx + r * 0.3f - r, cy), IM_COL32(200, 200, 200, 220));
        }
        if (revActive) ImGui::PopStyleColor(3);
      }
      ImGui::SameLine(0, gap);
    }

    // --- Play / Pause ---
    {
      bool isPlaying = !simState.isPaused && !simState.reversePlay;
      if (isPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.60f, 0.32f, 1.0f));
      }
      if (ImGui::Button("##playpause", ImVec2(btnW + 4 * s, btnH))) {
        simState.isPaused = !simState.isPaused;
        simState.rewinding = false;
        simState.reversePlay = false;
        simState.scrubAccumulator = 0.0f;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(simState.isPaused ? "Play [Space]" : "Pause [Space]");
      {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
        float r = 5.0f * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (simState.isPaused) {
          dl->AddTriangleFilled(ImVec2(cx - r * 0.6f, cy - r), ImVec2(cx - r * 0.6f, cy + r),
                                ImVec2(cx + r, cy), IM_COL32(220, 220, 220, 240));
        } else {
          float bw = 2.5f * s;
          float g = 1.5f * s;
          dl->AddRectFilled(ImVec2(cx - g - bw, cy - r), ImVec2(cx - g, cy + r),
                            IM_COL32(220, 220, 220, 240), 1.0f * s);
          dl->AddRectFilled(ImVec2(cx + g, cy - r), ImVec2(cx + g + bw, cy + r),
                            IM_COL32(220, 220, 220, 240), 1.0f * s);
        }
      }
      if (isPlaying) ImGui::PopStyleColor(3);
    }
    ImGui::SameLine(0, gap);

    // --- Step Forward ---
    if (ImGui::Button("##step_fwd", ImVec2(btnW, btnH))) {
      simState.isPaused = true;
      simState.stepFrame = true;
      simState.rewinding = false;
      simState.reversePlay = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Forward [.]");
    {
      ImVec2 p = ImGui::GetItemRectMin();
      ImVec2 sz = ImGui::GetItemRectSize();
      float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
      float r = 4.0f * s;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddTriangleFilled(ImVec2(cx - r, cy - r), ImVec2(cx - r, cy + r),
                            ImVec2(cx + r - 3 * s, cy),
                            IM_COL32(200, 200, 200, 220));
      dl->AddRectFilled(ImVec2(cx + r - 2 * s, cy - r), ImVec2(cx + r, cy + r),
                        IM_COL32(200, 200, 200, 220));
    }
    ImGui::SameLine(0, sepGap);

    // --- Timeline scrub ---
    // Compute right-side width: sepGap + time + sepGap + speed button + [gap + bake button]
    float timeTextW = ImGui::CalcTextSize("00:00.00").x;
    float speedBtnW = 52.0f * s;
    float rightControlsW = sepGap + timeTextW + sepGap + speedBtnW;
    if (snapshotsOn) rightControlsW += gap + btnW;
    float scrubWidth = ImGui::GetContentRegionAvail().x - rightControlsW;
    if (scrubWidth < 60.0f * s) scrubWidth = 60.0f * s;

    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.50f, 0.60f, 0.85f, 1.0f));
    if (snapshotsOn && !simState.timeHistory.empty()) {
      int histSize = static_cast<int>(simState.timeHistory.size());
      int scrubIdx =
          simState.historyIndex >= 0 ? simState.historyIndex : histSize - 1;
      ImGui::SetNextItemWidth(scrubWidth);
      if (ImGui::SliderInt("##timeline", &scrubIdx, 0, histSize - 1, "")) {
        simState.isPaused = true;
        simState.rewinding = true;
        simState.reversePlay = false;
        simState.historyIndex = scrubIdx;
        simState.currentTime = simState.timeHistory[scrubIdx];
        simState.snapshotScrubbed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Frame %d / %d", scrubIdx, histSize - 1);
      }
    } else {
      int zero = 0;
      ImGui::SetNextItemWidth(scrubWidth);
      ImGui::SliderInt("##timeline_empty", &zero, 0, 0, "");
    }
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0, sepGap);

    // --- Elapsed time ---
    {
      int minutes = static_cast<int>(simState.currentTime) / 60;
      int seconds = static_cast<int>(simState.currentTime) % 60;
      int millis = static_cast<int>(
          (simState.currentTime - static_cast<int>(simState.currentTime)) * 100);
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%02d", minutes, seconds, millis);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.75f, 0.85f, 1.0f));
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * s);
      ImGui::Text("%s", timeBuf);
      ImGui::PopStyleColor(1);
    }
    ImGui::SameLine(0, sepGap);

    // --- Speed button (opens popup) ---
    {
      char speedLabel[16];
      snprintf(speedLabel, sizeof(speedLabel), "%.2fx", simState.timeSpeed);
      if (ImGui::Button(speedLabel, ImVec2(52 * s, btnH))) {
        showSpeedPopup = !showSpeedPopup;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback Speed [+/-]");
    }

    if (showSpeedPopup) {
      float popW = 220 * s;
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      ImVec2 btnMin = ImGui::GetItemRectMin();
      float popX = btnMin.x;
      float popY = btnMin.y - speedPopupHeight - 4 * s;
      popX = glm::clamp(popX, vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - popW);
      popY = glm::max(popY, vp->WorkPos.y);
      ImGui::SetNextWindowPos(ImVec2(popX, popY));
      ImGui::SetNextWindowSize(ImVec2(popW, 0));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.16f, 0.98f));
      if (ImGui::Begin("##speed_popup", &showSpeedPopup,
                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 1.0f), "Speed");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6 * s));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##spd_slider", &simState.timeSpeed, 0.01f, 10.0f, "%.2fx");
        ImGui::Dummy(ImVec2(0, 8 * s));
        // Two rows of 3: 0.1 0.5 1.0 / 2.0 5.0 10.0
        float presets[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
        const char* presetLabels[] = {"0.1x", "0.5x", "1x", "2x", "5x", "10x"};
        float btnPresetW = (popW - ImGui::GetStyle().WindowPadding.x * 2.0f - gap * 2.0f) / 3.0f;
        for (int i = 0; i < 6; i++) {
          if (i == 3) { ImGui::Dummy(ImVec2(0, 4 * s)); }
          if (i > 0 && i != 3) ImGui::SameLine(0, gap);
          bool active = (std::abs(simState.timeSpeed - presets[i]) < 0.005f);
          if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.38f, 0.58f, 1.0f));
          if (ImGui::Button(presetLabels[i], ImVec2(btnPresetW, btnH)))
            simState.timeSpeed = presets[i];
          if (active) ImGui::PopStyleColor(1);
        }
        ImGui::Dummy(ImVec2(0, 8 * s));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.50f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6 * s));
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.55f, 1.0f), "Step Size");
        ImGui::Dummy(ImVec2(0, 2 * s));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##step_slider", &simState.stepSize, 0.001f, 0.1f, "%.3fs");
        ImGui::Dummy(ImVec2(0, 4 * s));
        speedPopupHeight = ImGui::GetWindowSize().y;
      }
      ImGui::End();
      ImGui::PopStyleColor(1);
    }

    // --- Bake button (snapshots only) ---
    if (snapshotsOn) {
      ImGui::SameLine(0, gap);
      if (simState.baked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.38f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.46f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.52f, 0.30f, 1.0f));
      }
      if (ImGui::Button("##bake_btn", ImVec2(btnW, btnH))) {
        showBakePopup = !showBakePopup;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(simState.baked ? "Baked" : "Bake Simulation");
      {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        float cx = p.x + sz.x * 0.5f, cy = p.y + sz.y * 0.5f;
        float r = 4.0f * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 col = simState.baked ? IM_COL32(120, 200, 120, 220) : IM_COL32(200, 200, 200, 220);
        dl->AddCircle(ImVec2(cx, cy), r, col, 0, 2.0f * s);
        dl->AddCircleFilled(ImVec2(cx, cy), r * 0.4f, col);
      }
      if (simState.baked) ImGui::PopStyleColor(3);

      if (showBakePopup) {
        float popW = 220 * s;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 btnMin = ImGui::GetItemRectMin();
        float popX = btnMin.x;
        float popY = btnMin.y - bakePopupHeight - 4 * s;
        popX = glm::clamp(popX, vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - popW);
        popY = glm::max(popY, vp->WorkPos.y);
        ImGui::SetNextWindowPos(ImVec2(popX, popY));
        ImGui::SetNextWindowSize(ImVec2(popW, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.16f, 0.98f));
        if (ImGui::Begin("##bake_popup", &showBakePopup,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
          ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 1.0f), "Bake");
          ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
          ImGui::Separator();
          ImGui::PopStyleColor();

          // --- Duration presets ---
          ImGui::Dummy(ImVec2(0, 6 * s));
          struct BakePreset { float duration; };
          BakePreset presets[] = {{5.0f}, {10.0f}, {30.0f}, {60.0f}};
          for (int i = 0; i < 4; i++) {
            if (i > 0) ImGui::Dummy(ImVec2(0, 4 * s));
            int frames = static_cast<int>(presets[i].duration / simState.stepSize);
            char label[48];
            snprintf(label, sizeof(label), "%.0fs  (%d frames)", presets[i].duration, frames);
            if (ImGui::Button(label, ImVec2(-1, btnH))) {
              simState.bakeDuration = presets[i].duration;
              simState.bakeRequested = true;
              showBakePopup = false;
              if (simState.bakeStats.hasData) showBakeStatsWindow = true;
            }
          }

          // --- Load Bake ---
          ImGui::Dummy(ImVec2(0, 10 * s));
          ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
          ImGui::Separator();
          ImGui::PopStyleColor();
          ImGui::Dummy(ImVec2(0, 8 * s));
          if (hasBakeFile) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.32f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.40f, 0.62f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.46f, 0.70f, 1.0f));
          }
          ImGui::BeginDisabled(!hasBakeFile);
          if (ImGui::Button("Load Bake", ImVec2(-1, btnH))) {
            simState.loadBakeRequested = true;
            showBakePopup = false;
          }
          ImGui::EndDisabled();
          if (hasBakeFile) {
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Load previously saved bake from disk.");
          } else {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
              ImGui::SetTooltip("No .worldbake file found for this world.");
          }

          // --- Performance Mode ---
          ImGui::Dummy(ImVec2(0, 10 * s));
          ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
          ImGui::Separator();
          ImGui::PopStyleColor();
          ImGui::Dummy(ImVec2(0, 8 * s));
          ImGui::Checkbox("Performance Mode", &simState.bakePerformanceMode);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bakes with no scene rendering for accurate\nphysics performance profiling.");
          if (simState.baked && simState.bakeStats.hasData) {
            ImGui::Dummy(ImVec2(0, 4 * s));
            if (ImGui::SmallButton("View Last Stats")) {
              showBakeStatsWindow = true;
              showBakePopup = false;
            }
          }
          ImGui::Dummy(ImVec2(0, 6 * s));
          bakePopupHeight = ImGui::GetWindowSize().y;
        }
        ImGui::End();
        ImGui::PopStyleColor(1);
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(9);
  ImGui::PopStyleVar(6);

  if (simState.isBaking) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float popW = 300.0f * s;
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + (vp->WorkSize.x - popW) * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.42f));
    ImGui::SetNextWindowSize(ImVec2(popW, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.97f));
    ImGui::Begin("##bake_progress", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 1.0f), "Baking Simulation...");
    ImGui::Spacing();

    float progress = (simState.bakeTotalSteps > 0)
        ? static_cast<float>(simState.bakeCurrentStep) / static_cast<float>(simState.bakeTotalSteps)
        : 0.0f;
    char progressLabel[64];
    snprintf(progressLabel, sizeof(progressLabel), "Frame %d / %d  (%.1f%%)",
             simState.bakeCurrentStep, simState.bakeTotalSteps, progress * 100.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.30f, 0.48f, 0.78f, 1.0f));
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 18.0f * s), progressLabel);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::End();
    ImGui::PopStyleColor(1);
  }

  if (showBakeStatsWindow && simState.bakeStats.hasData) {
    const BakeStats& st = simState.bakeStats;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float winW = 420.0f * s;
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + (vp->WorkSize.x - winW) * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.12f),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(winW, 0), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.98f));

    if (ImGui::Begin("Bake Performance Stats", &showBakeStatsWindow,
                     ImGuiWindowFlags_NoSavedSettings)) {

      auto statRow = [&](const char* label, const char* value, ImVec4 col = ImVec4(0.80f, 0.85f, 1.0f, 1.0f)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.58f, 0.68f, 1.0f));
        ImGui::Text("%-30s", label);
        ImGui::PopStyleColor();
        ImGui::SameLine(190.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%s", value);
        ImGui::PopStyleColor();
      };

      auto sectionHead = [&](const char* label) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.62f, 0.88f, 1.0f));
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
      };

      char buf[64];

      sectionHead("Scene");
      snprintf(buf, sizeof(buf), "%d", st.objectCount);
      statRow("Physics Objects", buf);
      snprintf(buf, sizeof(buf), "%d", st.totalSteps);
      statRow("Total Steps", buf);
      snprintf(buf, sizeof(buf), "%.3f s", st.stepSize);
      statRow("Step Size", buf);
      snprintf(buf, sizeof(buf), "%.1f s", st.simDuration);
      statRow("Sim Duration", buf);

      sectionHead("Wall Time");
      snprintf(buf, sizeof(buf), "%.2f ms", st.totalWallTimeMs);
      statRow("Total Bake Time", buf,
              st.totalWallTimeMs < 500.0 ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) :
              st.totalWallTimeMs < 2000.0 ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f) :
                                            ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
      snprintf(buf, sizeof(buf), "%.4f ms", st.avgStepMs);
      statRow("Avg Step Time", buf);
      snprintf(buf, sizeof(buf), "%.4f ms", st.minStepMs);
      statRow("Min Step Time", buf, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
      snprintf(buf, sizeof(buf), "%.4f ms", st.maxStepMs);
      statRow("Max Step Time", buf, ImVec4(1.0f, 0.75f, 0.4f, 1.0f));
      snprintf(buf, sizeof(buf), "%.4f ms", st.avgUiFrameMs);
      statRow("Avg UI Frame Time", buf);

      sectionHead("Throughput");
      snprintf(buf, sizeof(buf), "%.1f steps/s", st.stepsPerSecond);
      statRow("Steps / Second", buf, ImVec4(0.55f, 0.90f, 0.65f, 1.0f));
      snprintf(buf, sizeof(buf), "%.2fx real-time", st.simSecondsPerWallSecond);
      statRow("Sim Speed (vs real-time)", buf,
              st.simSecondsPerWallSecond >= 1.0 ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) :
                                                  ImVec4(1.0f, 0.5f, 0.4f, 1.0f));

      sectionHead("Step Breakdown (avg per step)");
      double totalAvg = st.avgSyncToMs + st.avgPhysStepMs + st.avgSyncFromMs + st.avgSnapshotMs;
      auto pct = [&](double v) -> double { return totalAvg > 0.0 ? (v / totalAvg * 100.0) : 0.0; };

      snprintf(buf, sizeof(buf), "%.5f ms  (%.1f%%)", st.avgSyncToMs, pct(st.avgSyncToMs));
      statRow("  Sync To Physics", buf);
      snprintf(buf, sizeof(buf), "%.5f ms  (%.1f%%)", st.avgPhysStepMs, pct(st.avgPhysStepMs));
      statRow("  Physics Step", buf);
      snprintf(buf, sizeof(buf), "%.5f ms  (%.1f%%)", st.avgSyncFromMs, pct(st.avgSyncFromMs));
      statRow("  Sync From Physics", buf);
      snprintf(buf, sizeof(buf), "%.5f ms  (%.1f%%)", st.avgSnapshotMs, pct(st.avgSnapshotMs));
      statRow("  Snapshot Save", buf);

      sectionHead("Collisions");
      if (st.collisionPairs.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
        ImGui::Text("  No collision pairs recorded.");
        ImGui::PopStyleColor();
      } else {
        // Header row
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.55f, 1.0f));
        ImGui::Text("  %-24s  %10s  %10s  %8s  %10s",
                    "Pair", "Checks", "Resolved", "Hit%", "Chk/Step");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        long long grandChecks = 0, grandResolved = 0;
        for (const auto& p : st.collisionPairs) { grandChecks += p.totalChecks; grandResolved += p.totalResolved; }

        for (const auto& p : st.collisionPairs) {
          double hitPct = p.totalChecks > 0
              ? 100.0 * static_cast<double>(p.totalResolved) / static_cast<double>(p.totalChecks) : 0.0;
          double chkPerStep = st.totalSteps > 0
              ? static_cast<double>(p.totalChecks) / st.totalSteps : 0.0;
          char rowbuf[128];
          snprintf(rowbuf, sizeof(rowbuf), "  %-24s  %10lld  %10lld  %7.1f%%  %9.1f",
                   p.pairName.c_str(), p.totalChecks, p.totalResolved, hitPct, chkPerStep);
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.85f, 1.0f, 1.0f));
          ImGui::TextUnformatted(rowbuf);
          ImGui::PopStyleColor();
        }

        // Totals row
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        double totalHitPct = grandChecks > 0
            ? 100.0 * static_cast<double>(grandResolved) / static_cast<double>(grandChecks) : 0.0;
        snprintf(buf, sizeof(buf), "%lld", grandChecks);
        statRow("  Total Checks", buf);
        snprintf(buf, sizeof(buf), "%lld", grandResolved);
        statRow("  Total Resolved", buf);
        snprintf(buf, sizeof(buf), "%.1f%%", totalHitPct);
        statRow("  Overall Hit Rate", buf);
      }

      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
      ImGui::Separator();
      ImGui::PopStyleColor();
      ImGui::Spacing();

      if (ImGui::Button("Copy to Clipboard", ImVec2(-1, btnH))) {
        char clip[4096];
        int n = 0;
        n += snprintf(clip + n, sizeof(clip) - n, "=== Bake Performance Stats ===\n");
        n += snprintf(clip + n, sizeof(clip) - n, "[Scene]\n");
        n += snprintf(clip + n, sizeof(clip) - n, "Physics Objects:           %d\n", st.objectCount);
        n += snprintf(clip + n, sizeof(clip) - n, "Total Steps:               %d\n", st.totalSteps);
        n += snprintf(clip + n, sizeof(clip) - n, "Step Size:                 %.3f s\n", st.stepSize);
        n += snprintf(clip + n, sizeof(clip) - n, "Sim Duration:              %.1f s\n", st.simDuration);
        n += snprintf(clip + n, sizeof(clip) - n, "[Wall Time]\n");
        n += snprintf(clip + n, sizeof(clip) - n, "Total Bake Time:           %.2f ms\n", st.totalWallTimeMs);
        n += snprintf(clip + n, sizeof(clip) - n, "Avg Step Time:             %.4f ms\n", st.avgStepMs);
        n += snprintf(clip + n, sizeof(clip) - n, "Min Step Time:             %.4f ms\n", st.minStepMs);
        n += snprintf(clip + n, sizeof(clip) - n, "Max Step Time:             %.4f ms\n", st.maxStepMs);
        n += snprintf(clip + n, sizeof(clip) - n, "Avg UI Frame Time:         %.4f ms\n", st.avgUiFrameMs);
        n += snprintf(clip + n, sizeof(clip) - n, "[Throughput]\n");
        n += snprintf(clip + n, sizeof(clip) - n, "Steps / Second:            %.1f\n", st.stepsPerSecond);
        n += snprintf(clip + n, sizeof(clip) - n, "Sim Speed (vs real-time):  %.2fx\n", st.simSecondsPerWallSecond);
        n += snprintf(clip + n, sizeof(clip) - n, "[Step Breakdown (avg per step)]\n");
        n += snprintf(clip + n, sizeof(clip) - n, "Sync To Physics:           %.5f ms  (%.1f%%)\n", st.avgSyncToMs, pct(st.avgSyncToMs));
        n += snprintf(clip + n, sizeof(clip) - n, "Physics Step:              %.5f ms  (%.1f%%)\n", st.avgPhysStepMs, pct(st.avgPhysStepMs));
        n += snprintf(clip + n, sizeof(clip) - n, "Sync From Physics:         %.5f ms  (%.1f%%)\n", st.avgSyncFromMs, pct(st.avgSyncFromMs));
        n += snprintf(clip + n, sizeof(clip) - n, "Snapshot Save:             %.5f ms  (%.1f%%)\n", st.avgSnapshotMs, pct(st.avgSnapshotMs));
        n += snprintf(clip + n, sizeof(clip) - n, "[Collisions]\n");
        n += snprintf(clip + n, sizeof(clip) - n, "%-26s  %10s  %10s  %8s  %10s\n",
                      "Pair", "Checks", "Resolved", "Hit%", "Chk/Step");
        long long gc = 0, gr = 0;
        for (const auto& p : st.collisionPairs) {
          gc += p.totalChecks; gr += p.totalResolved;
          double hp = p.totalChecks > 0 ? 100.0 * static_cast<double>(p.totalResolved) / static_cast<double>(p.totalChecks) : 0.0;
          double cs = st.totalSteps > 0 ? static_cast<double>(p.totalChecks) / st.totalSteps : 0.0;
          n += snprintf(clip + n, sizeof(clip) - n, "%-26s  %10lld  %10lld  %7.1f%%  %9.1f\n",
                        p.pairName.c_str(), p.totalChecks, p.totalResolved, hp, cs);
        }
        if (!st.collisionPairs.empty()) {
          double gHitPct = gc > 0 ? 100.0 * static_cast<double>(gr) / static_cast<double>(gc) : 0.0;
          n += snprintf(clip + n, sizeof(clip) - n, "%-26s  %10lld  %10lld  %7.1f%%\n",
                        "TOTAL", gc, gr, gHitPct);
        }
        ImGui::SetClipboardText(clip);
      }
      ImGui::Spacing();
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
  }
}

void Interface::renderObjectsMenu(Registry& registry) {
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

  const float s = currentScale;
  const float dragW = 180.0f * s;
  const float labelCol = 90.0f * s;
  const float listW = 160.0f * s;
  const float inspectorW = 220.0f * s;
  const float menuW = listW + 8 * s + inspectorW;
  const float totalH = 340.0f * s;

  // Force a minimum width for the menu popup
  ImGui::Dummy(ImVec2(menuW, 0));

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.44f, 0.52f, 1.0f));
  ImGui::Text("%d entities  |  %d objects  |  %d lights",
              static_cast<int>(entities.size()), meshCount, lightCount);
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  auto propRow = [&](const char* label, const char* fmt, ...) {
    fieldLabel(label);
    ImGui::SameLine(labelCol);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
  };

  hoveredEntity = INVALID_ENTITY;

  // --- Side-by-side: list left, inspector right ---
  ImGui::BeginChild("##obj_list_pane", ImVec2(listW, totalH), false);
  {
    if (!meshEntities.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.68f, 0.45f, 1.0f));
      ImGui::Text("Objects (%d)", meshCount);
      ImGui::PopStyleColor();
      ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.35f, 0.25f, 0.40f));
      ImGui::Separator();
      ImGui::PopStyleColor();
      for (Entity e : meshEntities) {
        auto* nameComp = registry.getComponent<NameComponent>(e);
        std::string label = (nameComp ? nameComp->name : "Unknown");

        ImGui::PushID(static_cast<int>(e));
        bool isSelected = (selectedEntity == e);

        // Draw cube icon
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.60f, 0.40f, 0.8f));
        ImGui::Text("#");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4 * s);

        if (ImGui::Selectable(label.c_str(), isSelected,
                              ImGuiSelectableFlags_None,
                              ImVec2(listW - 24 * s, 0))) {
          selectedEntity = isSelected ? INVALID_ENTITY : e;
        }
        if (ImGui::IsItemHovered()) hoveredEntity = e;
        ImGui::PopID();
      }
    }

    if (!lightEntities.empty()) {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.30f, 1.0f));
      ImGui::Text("Lights (%d)", lightCount);
      ImGui::PopStyleColor();
      ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.45f, 0.40f, 0.20f, 0.40f));
      ImGui::Separator();
      ImGui::PopStyleColor();
      for (Entity e : lightEntities) {
        auto* nameComp = registry.getComponent<NameComponent>(e);
        auto* light = registry.getComponent<LightComponent>(e);
        std::string label = (nameComp ? nameComp->name : "Unknown");

        ImGui::PushID(static_cast<int>(e));
        bool isSelected = (selectedEntity == e);

        // Sun/point icon
        if (light && light->type == LightType::Sun) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.30f, 0.8f));
          ImGui::Text("*");
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.30f, 0.8f));
          ImGui::Text("o");
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4 * s);

        if (ImGui::Selectable(label.c_str(), isSelected,
                              ImGuiSelectableFlags_None,
                              ImVec2(listW - 24 * s, 0))) {
          selectedEntity = isSelected ? INVALID_ENTITY : e;
        }
        if (ImGui::IsItemHovered()) hoveredEntity = e;
        ImGui::PopID();
      }
    }
  }
  ImGui::EndChild();

  ImGui::SameLine(0, 8 * s);

  // --- Inspector pane ---
  ImGui::BeginChild("##obj_inspector_pane", ImVec2(inspectorW, totalH), false);
  {
    if (selectedEntity == INVALID_ENTITY) {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.40f, 0.48f, 1.0f));
      ImGui::Text("Select an entity to inspect");
      ImGui::PopStyleColor();
    } else {
      auto* nameComp = registry.getComponent<NameComponent>(selectedEntity);
      std::string entityName = nameComp ? nameComp->name : "Unknown";
      bool isLight = registry.hasComponent<LightComponent>(selectedEntity);

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.72f, 0.95f, 1.0f));
      ImGui::Text("%s", entityName.c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 6 * s);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.40f, 0.48f, 1.0f));
      ImGui::Text("(%u)", selectedEntity);
      ImGui::PopStyleColor();
      ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
      ImGui::Separator();
      ImGui::PopStyleColor();
      ImGui::Spacing();

      ImGui::PushID(static_cast<int>(selectedEntity));

      if (registry.hasComponent<TransformComponent>(selectedEntity)) {
        auto* transform =
            registry.getComponent<TransformComponent>(selectedEntity);
        auto* light = registry.getComponent<LightComponent>(selectedEntity);
        bool isSun = light && light->type == LightType::Sun;

        if (!isSun) {
          fieldLabel("Position");
          ImGui::SetNextItemWidth(dragW);
          ImGui::DragFloat3("##pos", &transform->position.x, 0.05f, 0.0f, 0.0f,
                            "%.2f");
        }

        if (!isLight) {
          // Scale with link toggle
          fieldLabel("Scale");
          ImGui::SameLine(0, 4 * s);

          // Link icon button
          ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * s, 2 * s));
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.42f, 1.0f));
          {
            ImU32 linkCol = scaleLinked
                ? IM_COL32(110, 140, 200, 220)
                : IM_COL32(100, 100, 110, 140);
            if (ImGui::Button("##link_scale", ImVec2(14 * s, 14 * s))) {
              scaleLinked = !scaleLinked;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(scaleLinked ? "Uniform Scale (linked)" : "Per-axis Scale (unlinked)");
            // Draw link icon
            ImVec2 p = ImGui::GetItemRectMin();
            ImVec2 sz = ImGui::GetItemRectSize();
            float icx = p.x + sz.x * 0.5f, icy = p.y + sz.y * 0.5f;
            float ir = 3.0f * s;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (scaleLinked) {
              dl->AddCircle(ImVec2(icx - ir * 0.4f, icy), ir, linkCol, 0, 1.5f * s);
              dl->AddCircle(ImVec2(icx + ir * 0.4f, icy), ir, linkCol, 0, 1.5f * s);
            } else {
              dl->AddCircle(ImVec2(icx - ir * 0.7f, icy), ir * 0.7f, linkCol, 0, 1.5f * s);
              dl->AddCircle(ImVec2(icx + ir * 0.7f, icy), ir * 0.7f, linkCol, 0, 1.5f * s);
            }
          }
          ImGui::PopStyleColor(3);
          ImGui::PopStyleVar();

          if (scaleLinked) {
            float uniformScale = transform->scale.x;
            ImGui::SetNextItemWidth(dragW);
            if (ImGui::DragFloat("##scl_uniform", &uniformScale, 0.01f, 0.001f,
                                 100.0f, "%.3f")) {
              transform->scale = glm::vec3(uniformScale);
            }
          } else {
            ImGui::SetNextItemWidth(dragW);
            ImGui::DragFloat3("##scl", &transform->scale.x, 0.01f, 0.001f,
                              100.0f, "%.3f");
          }

          glm::vec3 euler =
              glm::degrees(glm::eulerAngles(transform->rotation));
          fieldLabel("Rotation");
          ImGui::SetNextItemWidth(dragW);
          if (ImGui::DragFloat3("##rot", &euler.x, 0.5f, -360.0f, 360.0f,
                                "%.1f")) {
            transform->rotation = glm::quat(glm::radians(euler));
          }
        }

        ImGui::Spacing();
      }

      if (isLight) {
        auto* light = registry.getComponent<LightComponent>(selectedEntity);
        if (light) {
          propRow("Type", "%s",
                  light->type == LightType::Sun ? "Sun" : "Point");

          if (light->type == LightType::Sun) {
            fieldLabel("Direction");
            ImGui::SetNextItemWidth(dragW);
            ImGui::DragFloat3("##ldir", &light->direction.x, 0.01f, -1.0f, 1.0f,
                              "%.3f");
          }

          fieldLabel("Color");
          ImGui::SetNextItemWidth(dragW);
          ImGui::ColorEdit3("##lcol", &light->color.x);

          fieldLabel("Intensity");
          ImGui::SetNextItemWidth(dragW);
          ImGui::DragFloat("##lint", &light->intensity, 0.01f, 0.0f, 100.0f,
                           "%.2f");

          if (light->type == LightType::Point) {
            ImGui::Spacing();
            fieldLabel("Attenuation");
            ImGui::SetNextItemWidth(dragW);
            ImGui::DragFloat("Constant##att", &light->constant, 0.01f, 0.0f,
                             10.0f, "%.3f");
            ImGui::SetNextItemWidth(dragW);
            ImGui::DragFloat("Linear##att", &light->linear, 0.001f, 0.0f, 1.0f,
                             "%.4f");
            ImGui::SetNextItemWidth(dragW);
            ImGui::DragFloat("Quadratic##att", &light->quadratic, 0.001f, 0.0f,
                             1.0f, "%.4f");
          }

          ImGui::Spacing();
          ImGui::Checkbox("Shadows", &light->castsShadows);
        }
      }

      // --- Info row for mesh objects ---
      if (registry.hasComponent<MeshComponent>(selectedEntity) ||
          registry.hasComponent<MaterialComponent>(selectedEntity) ||
          registry.hasComponent<RenderComponent>(selectedEntity)) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
      }

      if (registry.hasComponent<MeshComponent>(selectedEntity)) {
        const auto* meshComp =
            registry.getComponent<MeshComponent>(selectedEntity);
        propRow("Mesh", "#%u", meshComp->meshID);
      }
      if (registry.hasComponent<MaterialComponent>(selectedEntity)) {
        const auto* matComp =
            registry.getComponent<MaterialComponent>(selectedEntity);
        propRow("Material", "#%u", matComp->materialID);
      }
      if (registry.hasComponent<RenderComponent>(selectedEntity)) {
        auto* renderComp =
            registry.getComponent<RenderComponent>(selectedEntity);
        ImGui::Checkbox("Visible", &renderComp->visible);
      }

      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void Interface::renderSceneMenu(SceneSettings& sceneSettings,
                              MainPipeline* mainPipeline) {
  const float s = currentScale;
  sectionHeader("Background");

  ImGui::ColorPicker3("##clearcolor", &sceneSettings.clearColor[0],
                      ImGuiColorEditFlags_PickerHueWheel |
                          ImGuiColorEditFlags_NoSidePreview |
                          ImGuiColorEditFlags_NoInputs);
  ImGui::Spacing();
  fieldLabel("Clear Color");
  ImGui::ColorEdit3("##clearcolor_edit", &sceneSettings.clearColor[0],
                    ImGuiColorEditFlags_NoLabel);

  sectionHeader("Shading");

  const char* shadingItems[] = {"Phong", "Gouraud"};
  int currentShading =
      (mainPipeline->getShadingMode() == MainPipeline::ShadingMode::Phong) ? 0
                                                                           : 1;
  fieldLabel("Shading Mode");
  ImGui::SetNextItemWidth(180.0f * s);
  if (ImGui::Combo("##shading", &currentShading, shadingItems,
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
  fieldLabel("Render Mode");
  ImGui::SetNextItemWidth(180.0f * s);
  if (ImGui::Combo("##rendermode", &currentPolygon, polygonItems,
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

  const float s = currentScale;
  const float sliderW = 220.0f * s;

  sectionHeader("Color Grading");

  fieldLabel("Hue");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##hue", &config.hue, -1.0f, 1.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##hue")) {
    config.hue = 0.0f;
    changed = true;
  }

  fieldLabel("Saturation");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##saturation", &config.saturation, 0.0f, 2.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##sat")) {
    config.saturation = 1.0f;
    changed = true;
  }

  fieldLabel("Contrast");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##contrast", &config.contrast, 0.0f, 2.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##con")) {
    config.contrast = 1.0f;
    changed = true;
  }

  sectionHeader("Effects");

  fieldLabel("Chromatic Aberration");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##chromatic", &config.chromaticAberration, 0.0f, 0.05f, "%.4f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##ca")) {
    config.chromaticAberration = 0.003f;
    changed = true;
  }

  fieldLabel("Vignette");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##vignette", &config.vignetteStrength, 0.0f, 1.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##vig")) {
    config.vignetteStrength = 0.3f;
    changed = true;
  }

  fieldLabel("Sharpen");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##sharpen", &config.sharpenStrength, 0.0f, 1.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##shp")) {
    config.sharpenStrength = 0.3f;
    changed = true;
  }

  fieldLabel("Exposure");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##exposure", &config.exposure, 0.1f, 5.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##exp")) {
    config.exposure = 1.0f;
    changed = true;
  }

  fieldLabel("Gamma");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##gamma", &config.gamma, 0.5f, 3.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##gam")) {
    config.gamma = 1.0f;
    changed = true;
  }

  fieldLabel("Film Grain");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##filmgrain", &config.filmGrain, 0.0f, 0.3f, "%.3f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##grain")) {
    config.filmGrain = 0.0f;
    changed = true;
  }

  fieldLabel("Temperature");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##temperature", &config.temperature, -1.0f, 1.0f, "%.2f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##temp")) {
    config.temperature = 0.0f;
    changed = true;
  }

  ImGui::Spacing();

  if (ImGui::Checkbox("Toon Shader", &config.useToon)) {
    postProcessing->setToonMode(config.useToon);
    if (config.useToon) {
      config.usePixel = false;
      postProcessing->setPixelMode(false);
    }
    changed = true;
  }
  keybadge("K");

  if (ImGui::Checkbox("Pixel Art", &config.usePixel)) {
    postProcessing->setPixelMode(config.usePixel);
    if (config.usePixel) {
      config.useToon = false;
      postProcessing->setToonMode(false);
    }
    changed = true;
  }

  ImGui::BeginDisabled(!config.usePixel);
  fieldLabel("Pixel Size");
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::SliderFloat("##pixelres", &config.pixelResolution, 1.0f, 16.0f, "%.0f"))
    changed = true;
  ImGui::SameLine(0, 10 * s);
  if (ImGui::SmallButton("Reset##pxr")) {
    config.pixelResolution = 4.0f;
    changed = true;
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Spacing();

  if (ImGui::Button("Reset All", ImVec2(120 * s, 28 * s))) {
    config.hue = 0.0f;
    config.saturation = 1.0f;
    config.contrast = 1.0f;
    config.chromaticAberration = 0.003f;
    config.vignetteStrength = 0.3f;
    config.sharpenStrength = 0.3f;
    config.exposure = 1.0f;
    config.gamma = 1.0f;
    config.filmGrain = 0.0f;
    config.temperature = 0.0f;
    config.pixelResolution = 4.0f;
    config.useToon = false;
    config.usePixel = false;
    postProcessing->setToonMode(false);
    postProcessing->setPixelMode(false);
    changed = true;
  }

  if (changed) {
    postProcessing->setConfig(config);
  }
}

void Interface::renderSettingsMenu(SimulationState& simState) {
  const float s = currentScale;
  sectionHeader("Interface");

  const char* presetItems[] = {"Small", "Normal", "Large", "XL"};
  int currentPreset = static_cast<int>(generalSettings.scalePreset);
  fieldLabel("UI Scale");
  ImGui::SetNextItemWidth(140.0f * s);
  if (ImGui::Combo("##uiscale", &currentPreset, presetItems,
                   IM_ARRAYSIZE(presetItems))) {
    generalSettings.scalePreset = static_cast<UIScalePreset>(currentPreset);
    applyScalePreset();
  }

  ImGui::Checkbox("Show FPS", &generalSettings.showFPS);
  keybadge("F1");
  ImGui::Checkbox("Show Light Gizmos", &generalSettings.showLightGizmos);

  sectionHeader("Simulation");

  bool prevSnapshots = generalSettings.snapshotsEnabled;
  ImGui::Checkbox("Enable Snapshots", &generalSettings.snapshotsEnabled);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Disable to remove timeline/replay overhead.\nReloads the current scene.");
  }
  if (generalSettings.snapshotsEnabled != prevSnapshots) {
    simState.reloadRequested = true;
  }

  sectionHeader("Keyboard Shortcuts");

  const float keyColW = 120.0f * s;

  auto shortcutRow = [&](const char* key, const char* desc) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.44f, 0.52f, 1.0f));
    ImGui::Text("%s", key);
    ImGui::PopStyleColor();
    ImGui::SameLine(keyColW);
    ImGui::Text("%s", desc);
  };

  fieldLabel("General");
  ImGui::Spacing();
  shortcutRow("ESC", "Exit application");
  shortcutRow("F1", "Toggle FPS display");
  shortcutRow("F11", "Toggle fullscreen");

  ImGui::Spacing();
  fieldLabel("Simulation");
  ImGui::Spacing();
  shortcutRow("Space", "Pause / Resume");
  shortcutRow(".", "Step forward");
  shortcutRow(",", "Step backward / rewind");
  shortcutRow("R", "Restart simulation");
  shortcutRow("+", "Double speed");
  shortcutRow("-", "Half speed");

  ImGui::Spacing();
  fieldLabel("Rendering");
  ImGui::Spacing();
  shortcutRow("L", "Cycle shading mode");
  shortcutRow("K", "Toggle toon shader");
  shortcutRow("Tab", "Cycle render mode");
  shortcutRow("G", "Toggle wireframe overlay");

  ImGui::Spacing();
  fieldLabel("Camera");
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

void Interface::setCurrentWorldPath(const std::string& path) {
  // Check once for the bake file so the UI can enable/disable the button
  hasBakeFile = std::filesystem::exists(
      std::filesystem::path(path).replace_extension(".worldbake"));
}

void Interface::notifyBakeSaved() {
  hasBakeFile = true;
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.50f, 1.0f));
    ImGui::Text("No .world files found in '%s'",
                worldDirectory.c_str());
    ImGui::PopStyleColor();
  } else {
    for (const auto& path : worldFiles) {
      std::string displayName = std::filesystem::path(path).stem().string();
      bool isCurrent = (path == lastLoadedWorld);

      ImGui::PushID(path.c_str());
      if (isCurrent) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.70f, 0.95f, 1.0f));
      }
      if (ImGui::Selectable(displayName.c_str(), isCurrent, 0, ImVec2(0, 0))) {
        if (worldLoadCallback) {
          worldLoadCallback(path);
          lastLoadedWorld = path;
        }
      }
      if (isCurrent) {
        ImGui::PopStyleColor();
      }
      ImGui::SameLine(200);
      auto it = worldFileStats.find(path);
      if (it != worldFileStats.end()) {
        const auto& ws = it->second;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.50f, 1.0f));
        ImGui::Text("%d obj  %d lit  %d tex  %d mat", ws.objects,
                    ws.lights, ws.textures, ws.materials);
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }
  }

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  if (ImGui::Button("Refresh", ImVec2(100 * currentScale, 28 * currentScale))) {
    refreshWorldList();
  }
  ImGui::SameLine(0, 12);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.50f, 1.0f));
  ImGui::Text("%d world(s)", static_cast<int>(worldFiles.size()));
  ImGui::PopStyleColor();
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