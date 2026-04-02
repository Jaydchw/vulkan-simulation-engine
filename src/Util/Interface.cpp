#include "Interface.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../Rendering/MainPipeline.h"
#include "../Rendering/PostProcessing.h"
#include "Debug.h"
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
EnvironmentSettings& environmentSettings,
Registry& registry, MainPipeline* mainPipeline,
PostProcessing* postProcessing,
const PerformanceMetrics& perfMetrics,
NetworkManager* networkManager) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // Update performance history ring buffers each frame (even when menu is closed)
  if (generalSettings.perfProfilingEnabled) {
    perfFrameHistory[perfHistoryOffset] = perfMetrics.frameTimeMs;
    perfSimHistory[perfHistoryOffset]   = perfMetrics.simThreadTotalMs;
    perfHistoryOffset = (perfHistoryOffset + 1) % PERF_HISTORY_SIZE;
    if (perfHistoryCount < PERF_HISTORY_SIZE) ++perfHistoryCount;
  }

  // Tint the entire UI with the local peer colour when in a multi-peer session.
  // Each instance gets a distinct hue so you can tell windows apart at a glance.
  static const ImVec4 kPeerTints[5] = {
      {0.6f, 0.6f, 0.6f, 1.0f},
      {1.0f, 0.3f, 0.3f, 1.0f},
      {0.3f, 1.0f, 0.3f, 1.0f},
      {0.3f, 0.5f, 1.0f, 1.0f},
      {1.0f, 1.0f, 0.3f, 1.0f},
  };
  const bool applyPeerTint = networkManager &&
                             networkManager->isRunning() &&
                             networkManager->getConnectedPeerCount() >= 1 &&
                             networkManager->getLocalPeerID() >= 1 &&
                             networkManager->getLocalPeerID() <= 4;
  ImVec4 peerTintColor = {0,0,0,0};
  if (applyPeerTint) {
    peerTintColor = kPeerTints[networkManager->getLocalPeerID()];
    const ImVec4& pc = peerTintColor;
    auto blend = [&](ImGuiCol col, float t) {
      const ImVec4 base = ImGui::GetStyleColorVec4(col);
      return ImVec4(base.x*(1-t)+pc.x*t, base.y*(1-t)+pc.y*t,
                    base.z*(1-t)+pc.z*t, base.w);
    };
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, blend(ImGuiCol_MenuBarBg, 0.18f));
  }

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
      renderSceneMenu(sceneSettings, mainPipeline, simState);
      ImGui::EndMenu();
    }
    if (menuItem("Environment")) {
      renderEnvironmentMenu(environmentSettings, registry);
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
    if (menuItem("Cameras")) {
      renderCamerasMenu(registry);
      ImGui::EndMenu();
    }
    if (menuItem("Network")) {
      renderNetworkMenu(networkManager, simState);
      ImGui::EndMenu();
    }
    if (menuItem("Performance")) {
      renderPerformanceMenu(perfMetrics);
      ImGui::EndMenu();
    }
    if (Debug::isRuntimeEnabled() && menuItem("Debug")) {
      renderDebugMenu();
      ImGui::EndMenu();
    }

    if (generalSettings.showFPS) {
      const float fps = ImGui::GetIO().Framerate;
      const float dt  = ImGui::GetIO().DeltaTime;

      // Smoothed TX/RX KB/s via EMA
      static uint64_t prevTotalSent = 0, prevTotalRecv = 0;
      static float smoothTxKBps = 0.0f, smoothRxKBps = 0.0f;
      const int peerCount = networkManager ? networkManager->getConnectedPeerCount() : 0;
      if (networkManager && peerCount >= 1 && dt > 0.0f) {
        uint64_t totalSent = 0, totalRecv = 0;
        for (const auto& p : networkManager->getPeerSnapshot()) {
          totalSent += p.bytesSent;
          totalRecv += p.bytesReceived;
        }
        const float txKBps = static_cast<float>(totalSent - prevTotalSent) / (dt * 1024.0f);
        const float rxKBps = static_cast<float>(totalRecv - prevTotalRecv) / (dt * 1024.0f);
        prevTotalSent = totalSent;
        prevTotalRecv = totalRecv;
        constexpr float alpha = 0.1f;
        smoothTxKBps = smoothTxKBps * (1-alpha) + txKBps * alpha;
        smoothRxKBps = smoothRxKBps * (1-alpha) + rxKBps * alpha;
      } else if (peerCount == 0) {
        smoothTxKBps = smoothRxKBps = 0.0f;
        prevTotalSent = prevTotalRecv = 0;
      }

      // Build display strings
      char fpsText[48];
      snprintf(fpsText, sizeof(fpsText), "%.0f FPS  |  %.1f ms", fps, 1000.0f / fps);

      const bool showNet = networkManager && peerCount >= 1;
      char txStr[24]={}, rxStr[24]={}, lossStr[24]={}, latStr[24]={};
      float loss = 0.0f, lat = 0.0f;
      if (showNet) {
        snprintf(txStr,   sizeof(txStr),   "TX %.1f",   smoothTxKBps);
        snprintf(rxStr,   sizeof(rxStr),   "  RX %.1f KB/s", smoothRxKBps);
        loss = networkManager->simPacketLossPercent;
        lat  = networkManager->simExtraLatencyMs;
        if (loss > 0.0f) snprintf(lossStr, sizeof(lossStr), "  loss %.0f%%", loss);
        if (lat  > 0.0f) snprintf(latStr,  sizeof(latStr),  "  +%.0fms",     lat);
      }

      // Measure total width for right-alignment
      float totalWidth = ImGui::CalcTextSize(fpsText).x;
      if (showNet) {
        totalWidth += ImGui::CalcTextSize(txStr).x
                    + ImGui::CalcTextSize(rxStr).x
                    + ImGui::CalcTextSize(lossStr).x
                    + ImGui::CalcTextSize(latStr).x
                    + ImGui::CalcTextSize("  |  ").x;  // separator before FPS
      }

      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - totalWidth - 20.0f);

      if (showNet) {
        // TX — green
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s", txStr);
        ImGui::SameLine(0, 0);
        // RX — cyan-blue
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", rxStr);
        ImGui::SameLine(0, 0);
        // Loss — yellow → red
        if (lossStr[0]) {
          const ImVec4 lossCol = loss >= 50.0f ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                               : loss >= 20.0f ? ImVec4(1.0f, 0.7f, 0.2f, 1.0f)
                                               : ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
          ImGui::TextColored(lossCol, "%s", lossStr);
          ImGui::SameLine(0, 0);
        }
        // Latency — orange
        if (latStr[0]) {
          ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", latStr);
          ImGui::SameLine(0, 0);
        }
        // Separator
        ImGui::TextDisabled("  |  ");
        ImGui::SameLine(0, 0);
      }

      // FPS — green / yellow / red
      if (fps >= 60.0f)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", fpsText);
      else if (fps >= 30.0f)
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", fpsText);
      else
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", fpsText);
    }
    ImGui::EndMainMenuBar();
  }

  renderTransportBar(simState, applyPeerTint ? peerTintColor : ImVec4{0,0,0,0});

  if (simState.bakePerformanceMode && simState.baked && simState.bakeStats.hasData &&
      !simState.isBaking && simState.bakeCurrentStep == simState.bakeTotalSteps) {
    showBakeStatsWindow = true;
  }

  if (applyPeerTint) ImGui::PopStyleColor(1);
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

void Interface::renderTransportBar(SimulationState& simState, ImVec4 peerTint) {
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

  {
    ImVec4 barBg = {0.11f, 0.11f, 0.14f, 1.0f};
    if (peerTint.w > 0.0f) {
      constexpr float t = 0.18f;
      barBg = {barBg.x*(1-t)+peerTint.x*t, barBg.y*(1-t)+peerTint.y*t,
               barBg.z*(1-t)+peerTint.z*t, 1.0f};
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, barBg);
  }
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.28f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.23f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.34f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.32f, 0.42f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.55f, 0.80f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.65f, 0.90f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));

  if (ImGui::Begin("##transport_bar", nullptr, flags)) {
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
          fieldLabel("Scale");
          ImGui::SameLine(0, 4 * s);

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

      if (registry.hasComponent<MeshComponent>(selectedEntity) ||
          registry.hasComponent<RenderMaterialComponent>(selectedEntity) ||
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
      if (registry.hasComponent<RenderMaterialComponent>(selectedEntity)) {
        const auto* matComp =
            registry.getComponent<RenderMaterialComponent>(selectedEntity);
        propRow("Material", "#%u", matComp->renderMaterialID);
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
                              MainPipeline* mainPipeline,
                              SimulationState& simState) {
  const float s = currentScale;
  const float sliderW = 220.0f * s;
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

  sectionHeader("Simulation");

  fieldLabel("Sim Rate (Hz)");
  ImGui::SetNextItemWidth(sliderW);
  int simHz = (simState.stepSize > 0.0f)
                  ? static_cast<int>(1.0f / simState.stepSize + 0.5f)
                  : 60;
  if (ImGui::SliderInt("##simhz", &simHz, 10, 500, "%d Hz")) {
    if (simHz > 0)
      simState.stepSize = 1.0f / static_cast<float>(simHz);
  }

  fieldLabel("Max FPS");
  ImGui::SetNextItemWidth(sliderW);
  ImGui::SliderInt("##maxfps", &simState.maxFps, 0, 300,
                   simState.maxFps == 0 ? "Unlimited" : "%d");
}

void Interface::renderEnvironmentMenu(EnvironmentSettings& environmentSettings,
                                      Registry& registry) {
  const float s = currentScale;
  const float sliderW = 220.0f * s;

  sectionHeader("Wind");
  fieldLabel("Wind Vector");
  ImGui::SetNextItemWidth(sliderW);
  ImGui::DragFloat3("##env_wind", &environmentSettings.wind.x, 0.05f, -50.0f, 50.0f, "%.2f");

  fieldLabel("Wind Drag");
  ImGui::SetNextItemWidth(sliderW);
  ImGui::SliderFloat("##env_wind_drag", &environmentSettings.windDrag, 0.0f, 2.0f, "%.2f");

  fieldLabel("Wind Targets");
  int mode = (environmentSettings.windAffects == WindAffectsMode::AllObjects) ? 1 : 0;
  const char* modeItems[] = {"Cloths Only", "All Physics Objects"};
  ImGui::SetNextItemWidth(sliderW);
  if (ImGui::Combo("##env_wind_mode", &mode, modeItems, IM_ARRAYSIZE(modeItems))) {
    environmentSettings.windAffects = (mode == 1)
        ? WindAffectsMode::AllObjects
        : WindAffectsMode::ClothOnly;
  }

  ImGui::Spacing();
  sectionHeader("Presets");
  if (ImGui::Button("Calm", ImVec2(70.0f * s, 24.0f * s))) {
    environmentSettings.wind = glm::vec3(0.0f);
  }
  ImGui::SameLine();
  if (ImGui::Button("Breeze", ImVec2(70.0f * s, 24.0f * s))) {
    environmentSettings.wind = glm::vec3(2.0f, 0.0f, 0.8f);
  }
  ImGui::SameLine();
  if (ImGui::Button("Gust", ImVec2(70.0f * s, 24.0f * s))) {
    environmentSettings.wind = glm::vec3(6.0f, 0.8f, 2.5f);
  }

  ImGui::Spacing();
  sectionHeader("Coverage");
  fieldLabel("Cloths");
  ImGui::SameLine();
  ImGui::Text("%d", static_cast<int>(registry.allCloths().size()));
  fieldLabel("Simulated Bodies");
  ImGui::SameLine();
  ImGui::Text("%d", static_cast<int>(registry.allSimulated().size()));
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

  bool prevAffinity = simState.threadAffinityEnabled;
  ImGui::Checkbox("Enable Thread Affinity", &simState.threadAffinityEnabled);
  if (simState.threadAffinityEnabled != prevAffinity) {
    Debug::log(Debug::Category::THREADING,
               "Thread affinity toggle changed enabled=", simState.threadAffinityEnabled);
    simState.restartRequested = true;
  }

  sectionHeader("Diagnostics");
  bool runtimeLoggingEnabled = Debug::isRuntimeEnabled();
  if (ImGui::Checkbox("Enable Debug Logging", &runtimeLoggingEnabled)) {
    Debug::setRuntimeEnabled(runtimeLoggingEnabled);
  }
  if (runtimeLoggingEnabled) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 0.45f, 1.0f));
    ImGui::Text("Debug menu available in menu bar.");
    ImGui::PopStyleColor();
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

void Interface::setFBSceneDirectory(const std::string& dir) {
  fbSceneDirectory = dir;
  refreshWorldList();
}

void Interface::setCurrentWorldPath(const std::string& path) {
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
    bool descFound = false;
    while (std::getline(f, line)) {
      if (!descFound && !line.empty() && line[0] == '#') {
        std::string d = line.substr(1);
        auto start = d.find_first_not_of(" \t");
        if (start != std::string::npos) {
          stats.description = d.substr(start);
          descFound = true;
        }
      }
      if (line.find("BeginObject")   != std::string::npos) stats.objects++;
      if (line.find("BeginLight")    != std::string::npos) stats.lights++;
      if (line.find("BeginTexture")  != std::string::npos) stats.textures++;
      if (line.find("BeginMaterial") != std::string::npos) stats.materials++;
    }
    worldFileStats[path] = stats;
  }

  if (!fbSceneDirectory.empty()) {
    std::error_code ec;
    fbSceneFiles.clear();
    if (std::filesystem::exists(fbSceneDirectory, ec)) {
      for (const auto& entry : std::filesystem::directory_iterator(fbSceneDirectory, ec)) {
        auto ext = entry.path().extension();
        if (entry.is_regular_file() && (ext == ".fbscene" || ext == ".bin")) {
          fbSceneFiles.push_back(entry.path().string());
        }
      }
      std::sort(fbSceneFiles.begin(), fbSceneFiles.end());
    }
  }
}

void Interface::renderWorldsMenu() {
  const float s = currentScale;
  static char searchBuf[128] = {};

  // --- Reload button — always visible, disabled when no world is loaded ---
  {
    bool hasWorld = !lastLoadedWorld.empty();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.36f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.46f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.35f, 0.55f, 0.35f, 1.0f));
    if (!hasWorld) ImGui::BeginDisabled();
    if (ImGui::Button("  Reload Current Scene  ", ImVec2(-1, 28 * s))) {
      if (worldLoadCallback) worldLoadCallback(lastLoadedWorld);
    }
    if (!hasWorld) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (hasWorld && ImGui::IsItemHovered())
      ImGui::SetTooltip("Reload '%s'\nApplies to all connected peers.",
                        std::filesystem::path(lastLoadedWorld).stem().string().c_str());
  }

  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##wsearch", "Filter worlds...", searchBuf, sizeof(searchBuf));
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  std::string filter(searchBuf);
  for (char& c : filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  struct Category {
    const char* label;
    ImVec4      color;
  };
  static const Category kCats[] = {
    { "Core",      ImVec4(0.55f, 0.75f, 0.55f, 1.0f) },
    { "Cloth",     ImVec4(0.45f, 0.80f, 0.95f, 1.0f) },
    { "Spawners",  ImVec4(0.90f, 0.68f, 0.35f, 1.0f) },
    { "Animation", ImVec4(0.65f, 0.68f, 0.95f, 1.0f) },
    { "Stress",    ImVec4(0.85f, 0.40f, 0.40f, 1.0f) },
    { "Showcase",  ImVec4(0.80f, 0.75f, 0.50f, 1.0f) },
  };

  auto getCat = [&](const std::string& stem) -> int {
    std::string lower = stem;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("cloth") != std::string::npos) return 1;
    if (lower.find("spawner") != std::string::npos) return 2;
    if (lower.find("anim") == 0 || lower.find("animated") != std::string::npos) return 3;
    if (lower.find("stress") != std::string::npos || lower.find("big") != std::string::npos) return 4;
    if (lower.find("showcase") != std::string::npos ||
        lower.find("gallery") != std::string::npos ||
        lower.find("mix") != std::string::npos) return 5;
    return 0;
  };

  auto matchesFilter = [&](const std::string& stem) -> bool {
    if (filter.empty()) return true;
    std::string lower = stem;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find(filter) != std::string::npos;
  };

  // Render a row inside a 3-column table: selectable name | obj count | light count
  auto renderTableRow = [&](const std::string& path) {
    std::string stem = std::filesystem::path(path).stem().string();
    bool isCurrent   = (path == lastLoadedWorld);
    auto statsIt     = worldFileStats.find(path);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImGui::PushID(path.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, isCurrent ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f)
                                                   : ImVec4(0.88f, 0.88f, 0.92f, 1.0f));
    if (ImGui::Selectable(stem.c_str(), isCurrent,
                          ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 0))) {
      if (worldLoadCallback) {
        worldLoadCallback(path);
        lastLoadedWorld = path;
      }
    }
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.95f, 1.0f));
      ImGui::TextUnformatted(stem.c_str());
      ImGui::PopStyleColor();
      if (statsIt != worldFileStats.end() && !statsIt->second.description.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.70f, 0.76f, 1.0f));
        ImGui::TextUnformatted(statsIt->second.description.c_str());
        ImGui::PopStyleColor();
      }
      if (statsIt != worldFileStats.end()) {
        const auto& ws = statsIt->second;
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.50f, 0.58f, 1.0f));
        ImGui::Text("%d objects  %d lights  %d materials",
                    ws.objects, ws.lights, ws.materials);
        ImGui::PopStyleColor();
      }
      ImGui::EndTooltip();
    }

    if (statsIt != worldFileStats.end()) {
      const auto& ws = statsIt->second;
      ImGui::TableSetColumnIndex(1);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.40f, 0.50f, 1.0f));
      ImGui::Text("%d", ws.objects);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%d", ws.lights);
      ImGui::PopStyleColor();
    }
    ImGui::PopID();
  };

  constexpr ImGuiTableFlags kTableFlags =
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_NoHostExtendX;

  static int categoryColumns = 2;
  ImGui::SetNextItemWidth(160.0f * s);
  ImGui::SliderInt("Category Columns", &categoryColumns, 1, 3);
  ImGui::Spacing();

  if (worldFiles.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.50f, 1.0f));
    ImGui::Text("No .world files found in '%s'", worldDirectory.c_str());
    ImGui::PopStyleColor();
  } else {
    std::array<std::vector<const std::string*>, 6> grouped;
    for (const auto& path : worldFiles) {
      std::string stem = std::filesystem::path(path).stem().string();
      if (!matchesFilter(stem)) continue;
      int ci = getCat(stem);
      grouped[static_cast<size_t>(ci)].push_back(&path);
    }

    if (ImGui::BeginTable("##world_cat_cols", categoryColumns,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoHostExtendX)) {
      int renderedCats = 0;
      for (int ci = 0; ci < 6; ++ci) {
        if (grouped[ci].empty()) continue;
        ImGui::TableNextColumn();
        ImGui::PushID(ci);
        ImGui::PushStyleColor(ImGuiCol_Text, kCats[ci].color);
        ImGui::Text("%s  (%d)", kCats[ci].label, static_cast<int>(grouped[ci].size()));
        ImGui::PopStyleColor();
        if (ImGui::BeginTable("##wt", 3, kTableFlags)) {
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("Obj",  ImGuiTableColumnFlags_WidthFixed, 30.0f * s);
          ImGui::TableSetupColumn("Lit",  ImGuiTableColumnFlags_WidthFixed, 26.0f * s);
          for (const auto* pathPtr : grouped[ci])
            renderTableRow(*pathPtr);
          ImGui::EndTable();
        }
        ImGui::PopID();
        renderedCats++;
      }
      for (int i = renderedCats; i < categoryColumns; ++i)
        ImGui::TableNextColumn();
      ImGui::EndTable();
    }
  }

  // FlatBuffer Scenes
  if (!fbSceneFiles.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 0.65f, 1.0f));
    ImGui::Text("FlatBuffer  (%d)", static_cast<int>(fbSceneFiles.size()));
    ImGui::PopStyleColor();
    if (ImGui::BeginTable("##fbt", 1, ImGuiTableFlags_SizingStretchSame)) {
      for (const auto& path : fbSceneFiles) {
        std::string stem    = std::filesystem::path(path).stem().string();
        bool        isCurrent = (path == lastLoadedWorld);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(path.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, isCurrent ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f)
                                                       : ImVec4(0.75f, 0.85f, 0.65f, 1.0f));
        if (ImGui::Selectable(stem.c_str(), isCurrent)) {
          if (worldLoadCallback) { worldLoadCallback(path); lastLoadedWorld = path; }
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
  }

  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  if (ImGui::Button("Refresh", ImVec2(76.0f * s, 22.0f * s)))
    refreshWorldList();
  ImGui::SameLine(0, 10);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.40f, 0.48f, 1.0f));
  ImGui::Text("%d worlds  %d fb",
              static_cast<int>(worldFiles.size()),
              static_cast<int>(fbSceneFiles.size()));
  ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Network menu
// ---------------------------------------------------------------------------

void Interface::renderNetworkMenu(NetworkManager* nm, SimulationState& simState) {
  if (!nm) {
    ImGui::TextDisabled("NetworkManager not initialised");
    return;
  }

  const float s = currentScale;

  static const ImVec4 peerColors[5] = {
      {0.6f, 0.6f, 0.6f, 1.0f},
      {1.0f, 0.3f, 0.3f, 1.0f},
      {0.3f, 1.0f, 0.3f, 1.0f},
      {0.3f, 0.5f, 1.0f, 1.0f},
      {1.0f, 1.0f, 0.3f, 1.0f},
  };
  static const char* peerNames[5] = {
      "Unassigned", "Peer 1 (Red)", "Peer 2 (Green)", "Peer 3 (Blue)", "Peer 4 (Yellow)"
  };

  const bool    netRunning = nm->isRunning();
  const bool    connected  = nm->isConnected();
  const int     peerCount  = nm->getConnectedPeerCount();
  const uint8_t pid        = nm->getLocalPeerID();
  const uint8_t safeId     = (pid < 5) ? pid : 0;

  // ---- Status ----
  sectionHeader("Status");
  {
    if (!netRunning) {
      ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Networking Disabled");
    } else {
      ImVec4 sc = connected ? ImVec4(0.30f,0.90f,0.40f,1.0f) : ImVec4(0.80f,0.40f,0.40f,1.0f);
      ImGui::TextColored(sc, connected ? "Connected" : "Searching for peers...");
      ImGui::SameLine(0, 10*s);
      ImGui::TextDisabled("(%d peer%s)", peerCount, peerCount == 1 ? "" : "s");
    }

    ImGui::Spacing();
    fieldLabel("This Instance");
    ImGui::SameLine();
    ImGui::TextColored(peerColors[safeId], "%s", peerNames[safeId]);

    fieldLabel("Local IP");
    ImGui::SameLine();
    ImGui::Text("%s", nm->getLocalIP().c_str());

    fieldLabel("TCP Port");
    ImGui::SameLine();
    ImGui::Text("%d", (int)nm->getLocalTCPPort());

    fieldLabel("Instance ID");
    ImGui::SameLine();
    ImGui::TextDisabled("0x%08X", nm->getInstanceId());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Unique random ID generated at startup.\n"
                        "Used to assign deterministic Peer IDs across all instances.");
  }

  // ---- Ownership ----
  sectionHeader("Ownership");
  {
    const int owned = nm->getLocalOwnedCount();
    const int total = nm->getTotalManagedCount();
    fieldLabel("Simulating");
    ImGui::SameLine();
    if (total == 0) {
      ImGui::TextDisabled("(no scene loaded)");
    } else {
      ImGui::TextColored(peerColors[safeId], "%d", owned);
      ImGui::SameLine(0, 4*s);
      ImGui::TextDisabled("of %d physics objects", total);
    }
    if (peerCount > 0 && total > 0) {
      ImGui::SameLine(0, 12*s);
      ImGui::TextDisabled("(%d other%s simulating the rest)",
                          peerCount, peerCount == 1 ? "" : "s");
    }
  }

  // ---- Actions ----
  sectionHeader("Actions");
  {
    // ── Networking on/off toggle ──────────────────────────────────────────
    if (netRunning) {
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.15f, 0.15f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.20f, 0.20f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.72f, 0.26f, 0.26f, 1.0f));
      if (ImGui::Button("Disable Networking", ImVec2(160*s, 26*s)))
        nm->setNetworkEnabled(false);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.42f, 0.18f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.24f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f, 0.62f, 0.30f, 1.0f));
      if (ImGui::Button("Enable Networking", ImVec2(160*s, 26*s)))
        nm->setNetworkEnabled(true);
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(netRunning
          ? "Disconnect all peers, stop discovery.\nRuns fully locally until re-enabled."
          : "Start peer discovery and reconnect.");
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.36f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.46f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.35f, 0.55f, 0.35f, 1.0f));
    if (ImGui::Button("Reload Scene", ImVec2(130*s, 26*s)))
      simState.reloadRequested = true;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Reload the current world file on ALL connected peers.\n"
                        "Resets all physics objects to their starting positions.");

    ImGui::SameLine(0, 8*s);

    ImGui::BeginDisabled(!netRunning);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.32f, 0.32f, 0.42f, 1.0f));
    if (simState.isPaused) {
      if (ImGui::Button("Resume All", ImVec2(110*s, 26*s)))
        simState.isPaused = false;
    } else {
      if (ImGui::Button("Pause All", ImVec2(110*s, 26*s)))
        simState.isPaused = true;
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Pause or resume simulation on ALL connected peers.");
    ImGui::EndDisabled();
  }

  // ---- Peers ----
  sectionHeader("Peers");
  {
    auto peerList = nm->getPeerSnapshot();
    if (peerList.empty()) {
      ImGui::TextDisabled("  (none discovered yet)");
    }
    for (const auto& peer : peerList) {
      uint8_t sid = (peer.id < 5) ? peer.id : 0;

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      float sq = 10.0f * s;
      ImVec4 c = peerColors[sid];
      dl->AddRectFilled(p, {p.x+sq, p.y+sq},
                        IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),255));
      ImGui::Dummy({sq+4*s, sq});
      ImGui::SameLine(0, 4*s);

      ImVec4 textCol = peer.connected
          ? ImVec4(0.85f,0.85f,0.85f,1.0f)
          : ImVec4(0.5f,0.5f,0.5f,1.0f);
      ImGui::TextColored(textCol, "Peer %d — %s:%d",
                         (int)peer.id, peer.ip.c_str(), (int)peer.tcpPort);
      if (!peer.connected) {
        ImGui::SameLine(0, 6*s);
        ImGui::TextDisabled("[disconnected]");
      }

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f,0.48f,0.55f,1.0f));
      ImGui::Text("  Objects: %d   TX: %.1f KB   RX: %.1f KB",
                  peer.ownedObjects,
                  peer.bytesSent    / 1024.0f,
                  peer.bytesReceived / 1024.0f);
      ImGui::PopStyleColor();
    }
  }

  // ---- Visualisation ----
  sectionHeader("Visualisation");
  ImGui::Checkbox("Colour objects by owner", &nm->colorByOwner);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Override each object's material with its owner's colour.\n"
                      "Red=Peer1  Green=Peer2  Blue=Peer3  Yellow=Peer4\n"
                      "Syncs to all connected peers.");

  if (nm->colorByOwner) {
    ImGui::Spacing();
    static const ImVec4 legend[4] = {
        {1.0f,0.3f,0.3f,1.0f},
        {0.3f,1.0f,0.3f,1.0f},
        {0.3f,0.5f,1.0f,1.0f},
        {1.0f,1.0f,0.3f,1.0f},
    };
    static const char* lbl[4] = {"Peer 1","Peer 2","Peer 3","Peer 4"};
    for (int i = 0; i < 4; ++i) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      float sq = 12.0f * s;
      ImVec4 c = legend[i];
      dl->AddRectFilled(p,{p.x+sq,p.y+sq},
                        IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),255));
      ImGui::Dummy({sq+4*s, sq});
      ImGui::SameLine(0,4*s);
      ImGui::TextColored(c, "%s", lbl[i]);
      if (i < 3) ImGui::SameLine(0, 16*s);
    }
    ImGui::Spacing();
  }

  // ---- Send Rate ----
  sectionHeader("Send Rate");
  {
    ImGui::SetNextItemWidth(180.0f * s);
    ImGui::SliderFloat("##networkSendHz", &nm->networkSendHz, 1.0f, 120.0f, "%.0f Hz");
    ImGui::SameLine();
    ImGui::TextDisabled("physics tick rate");
  }

  // ---- Network Conditions ----
  sectionHeader("Conditions");
  {
    fieldLabel("Packet Loss");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * s);
    ImGui::SliderFloat("##pktLoss", &nm->simPacketLossPercent, 0.0f, 100.0f, "%.0f%%");

    fieldLabel("Extra Latency");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * s);
    ImGui::SliderFloat("##latency", &nm->simExtraLatencyMs, 0.0f, 500.0f, "%.0f ms");

    fieldLabel("Bandwidth Cap");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * s);
    if (nm->simBandwidthLimitKBps <= 0.0f)
      ImGui::SliderFloat("##bwcap", &nm->simBandwidthLimitKBps, 0.0f, 1000.0f, "Unlimited");
    else
      ImGui::SliderFloat("##bwcap", &nm->simBandwidthLimitKBps, 0.0f, 1000.0f, "%.0f KB/s");
    ImGui::Spacing();
  }

  // ---- Stats ----
  sectionHeader("Stats");
  {
    auto peerList = nm->getPeerSnapshot();
    uint64_t totalSent = 0, totalRecv = 0;
    for (const auto& p : peerList) { totalSent += p.bytesSent; totalRecv += p.bytesReceived; }

    auto fmtBytes = [](uint64_t b) -> std::string {
      char buf[32];
      if (b < 1024)            snprintf(buf, sizeof(buf), "%llu B",   (unsigned long long)b);
      else if (b < 1024*1024)  snprintf(buf, sizeof(buf), "%.2f KB",  b / 1024.0);
      else                     snprintf(buf, sizeof(buf), "%.2f MB",  b / (1024.0*1024.0));
      return buf;
    };

    fieldLabel("Total TX");
    ImGui::SameLine(); ImGui::Text("%s", fmtBytes(totalSent).c_str());
    fieldLabel("Total RX");
    ImGui::SameLine(); ImGui::Text("%s", fmtBytes(totalRecv).c_str());

    ImGui::Spacing();
    fieldLabel("Protocol");
    ImGui::SameLine(); ImGui::TextDisabled("UDP/45000 discovery + TCP/45001-45020 data");
    fieldLabel("Send rate");
    ImGui::SameLine(); ImGui::TextDisabled("%.0f Hz  (position + orientation, owned objects only)", nm->networkSendHz);
  }
}

void Interface::renderCamerasMenu(Registry& registry) {
  const auto& camerasMap = registry.allCameras();
  const float s = currentScale;

  if (camerasMap.empty()) {
    ImGui::TextDisabled("No cameras in scene");
    return;
  }

  std::vector<Entity> sorted;
  sorted.reserve(camerasMap.size());
  for (const auto& [e, _] : camerasMap) sorted.push_back(e);
  std::sort(sorted.begin(), sorted.end());

  ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 1.0f), "Scene Cameras");
  ImGui::Separator();
  ImGui::TextDisabled("Keys 1-9 switch cameras");
  ImGui::Spacing();

  int displayIndex = 0;
  for (Entity entity : sorted) {
    const NameComponent* nc = registry.getComponent<NameComponent>(entity);
    const char* name = nc ? nc->name.c_str() : "Unnamed Camera";
    const bool isActive = (displayIndex == activeCameraIdx);

    if (isActive) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.85f, 0.55f, 1.0f));
    char label[128];
    if (displayIndex < 9)
      snprintf(label, sizeof(label), "[%d] %s", displayIndex + 1, name);
    else
      snprintf(label, sizeof(label), "    %s", name);

    if (ImGui::MenuItem(label, nullptr, isActive)) {
      cameraSwitchTarget = displayIndex;
      cameraSwitchPending = true;
    }
    if (isActive) ImGui::PopStyleColor();
    displayIndex++;
  }

  // Settings for the active camera
  if (activeCameraIdx >= 0 && activeCameraIdx < static_cast<int>(sorted.size())) {
    Entity activeCamEntity = sorted[activeCameraIdx];
    CameraComponent* cam = registry.getComponent<CameraComponent>(activeCamEntity);
    const NameComponent* nc = registry.getComponent<NameComponent>(activeCamEntity);
    const char* name = nc ? nc->name.c_str() : "Camera";

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 1.0f), "%s", name);
    ImGui::Spacing();

    if (cam) {
      const float itemW = 200.0f * s;

      // Type toggle
      bool isPerspective = (cam->type == CameraType::Perspective);
      if (ImGui::RadioButton("Perspective", isPerspective))  cam->type = CameraType::Perspective;
      ImGui::SameLine();
      if (ImGui::RadioButton("Orthographic", !isPerspective)) cam->type = CameraType::Orthographic;

      ImGui::Spacing();

      if (cam->type == CameraType::Perspective) {
        ImGui::SetNextItemWidth(itemW);
        ImGui::SliderFloat("FOV", &cam->fov, 5.0f, 170.0f, "%.1f deg");
      } else {
        ImGui::SetNextItemWidth(itemW);
        ImGui::SliderFloat("Ortho Size", &cam->orthographicSize, 1.0f, 500.0f, "%.1f");
      }

      ImGui::SetNextItemWidth(itemW);
      ImGui::SliderFloat("Near", &cam->nearPlane, 0.01f, 10.0f, "%.2f");

      ImGui::SetNextItemWidth(itemW);
      ImGui::SliderFloat("Far", &cam->farPlane, 100.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    }
  }
}

void Interface::renderPerformanceMenu(const PerformanceMetrics& m) {
  const float s = currentScale;
  const float menuW = 420.0f * s;
  ImGui::SetNextItemWidth(menuW);

  // ── Enable / Disable toggle ───────────────────────────────────────────────
  bool enabled = generalSettings.perfProfilingEnabled;
  if (ImGui::Checkbox("Enable Profiling", &enabled)) {
    generalSettings.perfProfilingEnabled = enabled;
    if (enabled) {
      // Reset history so stale zeros don't pollute the graphs
      std::fill(perfFrameHistory, perfFrameHistory + PERF_HISTORY_SIZE, 0.0f);
      std::fill(perfSimHistory,   perfSimHistory   + PERF_HISTORY_SIZE, 0.0f);
      perfHistoryOffset = 0;
      perfHistoryCount  = 0;
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Disabling profiling removes all timing overhead\nand stops updating history graphs.");

  if (!enabled) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::TextWrapped("Profiling is disabled. Enable above to collect metrics.");
    ImGui::PopStyleColor();
    return;
  }

  ImGui::SameLine(0, 16.0f * s);
  if (ImGui::SmallButton("Reset History")) {
    std::fill(perfFrameHistory, perfFrameHistory + PERF_HISTORY_SIZE, 0.0f);
    std::fill(perfSimHistory,   perfSimHistory   + PERF_HISTORY_SIZE, 0.0f);
    perfHistoryOffset = 0;
    perfHistoryCount  = 0;
  }

  const ImVec4 kDim    = {0.55f, 0.58f, 0.68f, 1.0f};
  const ImVec4 kVal    = {0.90f, 0.92f, 1.00f, 1.0f};
  const ImVec4 kGood   = {0.30f, 0.90f, 0.35f, 1.0f};
  const ImVec4 kWarn   = {0.95f, 0.85f, 0.20f, 1.0f};
  const ImVec4 kBad    = {0.95f, 0.35f, 0.25f, 1.0f};
  const ImVec4 kAccent = {0.50f, 0.62f, 0.88f, 1.0f};

  // Compute min / max from the valid portion of the ring buffer
  const int validCount = perfHistoryCount;
  float frameMin = 0.0f, frameMax = 0.0f;
  float simMin   = 0.0f, simMax   = 0.0f;
  if (validCount > 0) {
    frameMin = frameMax = perfFrameHistory[0];
    simMin   = simMax   = perfSimHistory[0];
    for (int i = 1; i < validCount; ++i) {
      if (perfFrameHistory[i] < frameMin) frameMin = perfFrameHistory[i];
      if (perfFrameHistory[i] > frameMax) frameMax = perfFrameHistory[i];
      if (perfSimHistory[i]   < simMin)   simMin   = perfSimHistory[i];
      if (perfSimHistory[i]   > simMax)   simMax   = perfSimHistory[i];
    }
  }
  float frameAvg = 0.0f;
  if (validCount > 0) {
    for (int i = 0; i < validCount; ++i) frameAvg += perfFrameHistory[i];
    frameAvg /= static_cast<float>(validCount);
  }

  // Helper: colored progress-bar row inside a 3-column table
  // refMs is the "100%" reference (typically 16.67ms = 60fps budget)
  auto timingRow = [&](const char* label, float ms, float refMs) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(kDim, "%s", label);
    ImGui::TableSetColumnIndex(1);
    char buf[16]; snprintf(buf, sizeof(buf), "%.2f ms", ms);
    ImGui::TextColored(kVal, "%s", buf);
    ImGui::TableSetColumnIndex(2);
    float frac = (refMs > 0.0f) ? std::min(ms / refMs, 1.0f) : 0.0f;
    ImVec4 col = (frac < 0.5f) ? kGood : (frac < 0.8f) ? kWarn : kBad;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.21f, 1.0f));
    char pbId[40]; snprintf(pbId, sizeof(pbId), "##pb_%s", label);
    ImGui::ProgressBar(frac, ImVec2(-1.0f, 8.0f * s), "");
    ImGui::PopStyleColor(2);
  };

  // ── FRAME TIMING ─────────────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::Text("FRAME TIMING");
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  const float fps = (m.frameTimeMs > 0.0f) ? (1000.0f / m.frameTimeMs) : 0.0f;
  ImVec4 fpsCol = (fps >= 60.0f) ? kGood : (fps >= 30.0f) ? kWarn : kBad;
  ImGui::TextColored(fpsCol, "%.1f FPS", fps);
  ImGui::SameLine(0, 16.0f * s);
  ImGui::TextColored(kVal, "%.2f ms/frame", m.frameTimeMs);
  if (validCount > 0) {
    ImGui::SameLine(0, 16.0f * s);
    ImGui::TextColored(kDim, "min %.2f  max %.2f  avg %.2f ms",
                       frameMin, frameMax, frameAvg);
  }

  {
    char overlay[32]; snprintf(overlay, sizeof(overlay), "%.2f ms", m.frameTimeMs);
    float graphH = 50.0f * s;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.45f, 0.75f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   ImVec4(0.10f, 0.10f, 0.14f, 1.0f));
    ImGui::PlotLines("##frh", perfFrameHistory, PERF_HISTORY_SIZE, perfHistoryOffset,
                     overlay, 0.0f, std::max(frameMax * 1.2f, 33.33f), ImVec2(menuW, graphH));
    ImGui::PopStyleColor(2);
  }

  ImGui::Spacing();
  if (ImGui::BeginTable("##frameBreak", 3,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX)) {
    ImGui::TableSetupColumn("Label",   ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Bar",     ImGuiTableColumnFlags_WidthStretch, 2.5f);
    constexpr float kRef = 16.67f;  // 60fps budget
    timingRow("GPU Wait",           m.gpuWaitMs,         kRef);
    timingRow("Uniform Buffer",     m.uniformBufferMs,   kRef);
    timingRow("Command Buffer",     m.commandBufferMs,   kRef);
    timingRow("Interface Render",   m.interfaceRenderMs, kRef);
    // Total separator row
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(kDim, "Total");
    ImGui::TableSetColumnIndex(1);
    char tot[16]; snprintf(tot, sizeof(tot), "%.2f ms", m.frameTimeMs);
    ImGui::TextColored(kVal, "%s", tot);
    ImGui::EndTable();
  }

  // ── SIMULATION THREAD ────────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::Text("SIMULATION THREAD");
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  ImGui::TextColored(kVal, "%.2f ms / step", m.simThreadTotalMs);
  if (validCount > 0) {
    ImGui::SameLine(0, 16.0f * s);
    ImGui::TextColored(kDim, "min %.2f  max %.2f ms", simMin, simMax);
  }

  {
    char overlay[32]; snprintf(overlay, sizeof(overlay), "%.2f ms", m.simThreadTotalMs);
    float graphH = 50.0f * s;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.65f, 0.45f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   ImVec4(0.10f, 0.10f, 0.14f, 1.0f));
    ImGui::PlotLines("##simh", perfSimHistory, PERF_HISTORY_SIZE, perfHistoryOffset,
                     overlay, 0.0f, std::max(simMax * 1.2f, 16.67f), ImVec2(menuW, graphH));
    ImGui::PopStyleColor(2);
  }

  ImGui::Spacing();
  if (ImGui::BeginTable("##simBreak", 3,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX)) {
    ImGui::TableSetupColumn("Label",   ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Bar",     ImGuiTableColumnFlags_WidthStretch, 2.5f);
    const float kSimRef = std::max(m.simThreadTotalMs * 1.05f, 1.0f);
    timingRow("Physics SyncTo",   m.physSyncToMs,   kSimRef);
    timingRow("Physics Step",     m.physStepMs,     kSimRef);
    timingRow("Physics SyncFrom", m.physSyncFromMs, kSimRef);
    timingRow("Animation",        m.animationMs,    kSimRef);
    timingRow("Spawner",          m.spawnerMs,      kSimRef);
    timingRow("Snapshot",         m.snapshotMs,     kSimRef);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(kDim, "Total");
    ImGui::TableSetColumnIndex(1);
    char tot[16]; snprintf(tot, sizeof(tot), "%.2f ms", m.simThreadTotalMs);
    ImGui::TextColored(kVal, "%s", tot);
    ImGui::EndTable();
  }

  // ── ECS & MEMORY ─────────────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::Text("ECS & MEMORY");
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
  ImGui::Separator();
  ImGui::PopStyleColor();
  ImGui::Spacing();

  // Two-column entity count table
  if (ImGui::BeginTable("##ecsCounts", 4,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoHostExtendX)) {
    auto countRow = [&](const char* la, int va, const char* lb, int vb) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextColored(kDim, "%s", la);
      ImGui::TableSetColumnIndex(1); ImGui::TextColored(kVal, "%d", va);
      ImGui::TableSetColumnIndex(2); ImGui::TextColored(kDim, "%s", lb);
      ImGui::TableSetColumnIndex(3); ImGui::TextColored(kVal, "%d", vb);
    };
    countRow("Entities",         m.entityCount,   "Simulated Bodies", m.simBodyCount);
    countRow("Colliders",        m.colliderCount, "Lights",           m.lightCount);
    countRow("Spawners",         m.spawnerCount,  "Animated",         m.animCount);
    ImGui::EndTable();
  }

  ImGui::Spacing();

  // Component store size estimates
  // sizeof approx: Name ~32B, Transform ~40B, Simulated ~64B, Collider ~48B
  // unordered_map node overhead on MSVC: ~64B per element
  constexpr size_t kMapOverhead = 64;
  size_t nameKB      = (size_t)m.entityCount   * (32  + kMapOverhead) / 1024;
  size_t transformKB = (size_t)m.entityCount   * (40  + kMapOverhead) / 1024;
  size_t simulatedKB = (size_t)m.simBodyCount  * (64  + kMapOverhead) / 1024;
  size_t colliderKB  = (size_t)m.colliderCount * (48  + kMapOverhead) / 1024;
  size_t lightKB     = (size_t)m.lightCount    * (76  + kMapOverhead) / 1024;
  size_t totalEstKB  = nameKB + transformKB + simulatedKB + colliderKB + lightKB;

  ImGui::TextColored(kDim, "Component Storage (estimated heap)");
  if (ImGui::BeginTable("##compMem", 4,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoHostExtendX)) {
    auto memRow = [&](const char* la, size_t va, const char* lb, size_t vb) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextColored(kDim, "%s", la);
      ImGui::TableSetColumnIndex(1); ImGui::TextColored(kVal, "~%zu KB", va);
      ImGui::TableSetColumnIndex(2); ImGui::TextColored(kDim, "%s", lb);
      ImGui::TableSetColumnIndex(3); ImGui::TextColored(kVal, "~%zu KB", vb);
    };
    memRow("Names",     nameKB,      "Transforms", transformKB);
    memRow("Simulated", simulatedKB, "Colliders",  colliderKB);
    memRow("Lights",    lightKB,     "Total est.", totalEstKB);
    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextColored(kDim, "Timeline Snapshots");
  ImGui::SameLine(0, 8.0f * s);
  ImGui::TextColored(kVal, "%d", m.snapshotCount);
  ImGui::SameLine(0, 8.0f * s);
  if (m.snapshotMemKB < 1024)
    ImGui::TextColored(kDim, "(~%zu KB)", m.snapshotMemKB);
  else
    ImGui::TextColored(kDim, "(~%.1f MB)", static_cast<float>(m.snapshotMemKB) / 1024.0f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Estimate: snapshotCount * entityCount * 96 bytes\n"
                      "(EntitySnapshot = 24B data + ~72B map overhead)");

  // ── NETWORK ──────────────────────────────────────────────────────────────
  if (m.connectedPeers > 0) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::Text("NETWORK");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.28f, 0.38f, 0.60f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::TextColored(kDim, "Peers");
    ImGui::SameLine(0, 6.0f * s);
    ImGui::TextColored(kVal, "%d", m.connectedPeers);

    ImGui::SameLine(0, 20.0f * s);
    ImGui::TextColored(kDim, "TX");
    ImGui::SameLine(0, 6.0f * s);
    ImGui::TextColored(kGood, "%.1f KB/s", m.txKBps);

    ImGui::SameLine(0, 20.0f * s);
    ImGui::TextColored(kDim, "RX");
    ImGui::SameLine(0, 6.0f * s);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%.1f KB/s", m.rxKBps);

    ImGui::Spacing();
    ImGui::TextColored(kDim, "Packet Loss");
    ImGui::SameLine(0, 6.0f * s);
    {
      ImVec4 lc = (m.packetLoss >= 20.0f) ? kBad : (m.packetLoss > 0.0f) ? kWarn : kGood;
      ImGui::TextColored(lc, "%.1f%%", m.packetLoss);
    }
    ImGui::SameLine(0, 20.0f * s);
    ImGui::TextColored(kDim, "Sim Latency");
    ImGui::SameLine(0, 6.0f * s);
    ImGui::TextColored(kWarn, "+%.0f ms", m.latencyMs);
  }
}

void Interface::renderDebugMenu() {
  const float s = currentScale;
  const float menuW = 340.0f * s;
  ImGui::Dummy(ImVec2(menuW, 0));

  static const char* verbosityItems[] = {"Low", "Medium", "High", "Trace"};
  int v = static_cast<int>(Debug::getVerbosity());
  ImGui::SeparatorText("Verbosity");
  ImGui::SetNextItemWidth(220.0f * s);
  if (ImGui::Combo("Level##dbg_verbosity", &v, verbosityItems,
                   IM_ARRAYSIZE(verbosityItems))) {
    Debug::setVerbosity(static_cast<Debug::Verbosity>(v));
  }

  ImGui::SeparatorText("Presets");
  if (ImGui::Button("Enable All##dbg")) Debug::setAllEnabled(true);
  ImGui::SameLine();
  if (ImGui::Button("Disable All##dbg")) Debug::setAllEnabled(false);
  ImGui::SameLine();
  if (ImGui::Button("Core##dbg")) {
    Debug::setAllEnabled(false);
    Debug::setEnabled(Debug::Category::MAIN, true);
    Debug::setEnabled(Debug::Category::APP_LIFECYCLE, true);
    Debug::setEnabled(Debug::Category::VULKAN, true);
    Debug::setEnabled(Debug::Category::RENDERING, true);
    Debug::setEnabled(Debug::Category::NETWORK, true);
    Debug::setEnabled(Debug::Category::PHYSICS, true);
  }

  auto catCheckbox = [&](Debug::Category cat) {
    bool enabled = Debug::isEnabled(cat);
    if (ImGui::Checkbox(Debug::categoryDisplayName(cat), &enabled)) {
      Debug::setEnabled(cat, enabled);
    }
  };

  auto groupHeader = [&](const char* label, ImVec4 col) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
  };

  groupHeader("Core", ImVec4(0.70f, 0.70f, 1.00f, 1.0f));
  catCheckbox(Debug::Category::MAIN);
  catCheckbox(Debug::Category::APP_LIFECYCLE);
  catCheckbox(Debug::Category::CONFIG);
  catCheckbox(Debug::Category::ECS);
  catCheckbox(Debug::Category::THREADING);
  catCheckbox(Debug::Category::PERFORMANCE);
  catCheckbox(Debug::Category::FILE_IO);

  groupHeader("Camera, Input, UI", ImVec4(0.70f, 0.70f, 1.00f, 1.0f));
  catCheckbox(Debug::Category::CAMERA);
  catCheckbox(Debug::Category::CAMERA_VERBOSE);
  catCheckbox(Debug::Category::INPUT);
  catCheckbox(Debug::Category::INPUT_VERBOSE);
  catCheckbox(Debug::Category::UI);
  catCheckbox(Debug::Category::UI_LAYOUT);

  groupHeader("Rendering", ImVec4(0.40f, 0.85f, 1.00f, 1.0f));
  catCheckbox(Debug::Category::RENDERING);
  catCheckbox(Debug::Category::RENDERING_FRAME);
  catCheckbox(Debug::Category::RENDERING_CULLING);
  catCheckbox(Debug::Category::RENDERING_BATCH);
  catCheckbox(Debug::Category::RENDERING_SYNC);
  catCheckbox(Debug::Category::GIZMO);

  groupHeader("Vulkan", ImVec4(0.40f, 0.85f, 1.00f, 1.0f));
  catCheckbox(Debug::Category::VULKAN);
  catCheckbox(Debug::Category::VULKAN_MEMORY);
  catCheckbox(Debug::Category::VULKAN_SWAPCHAIN);
  catCheckbox(Debug::Category::VULKAN_PIPELINE);
  catCheckbox(Debug::Category::VULKAN_DESCRIPTORS);
  catCheckbox(Debug::Category::VULKAN_COMMANDS);

  groupHeader("Scene & Resources", ImVec4(0.50f, 1.00f, 0.70f, 1.0f));
  catCheckbox(Debug::Category::SCENE);
  catCheckbox(Debug::Category::SCENE_LOADER);
  catCheckbox(Debug::Category::WORLD);
  catCheckbox(Debug::Category::SKYBOX);
  catCheckbox(Debug::Category::SHADOWS);
  catCheckbox(Debug::Category::LIGHTS);
  catCheckbox(Debug::Category::MATERIALS);
  catCheckbox(Debug::Category::TEXTURE);
  catCheckbox(Debug::Category::MESH);
  catCheckbox(Debug::Category::OBJECTS);
  catCheckbox(Debug::Category::POSTPROCESSING);
  catCheckbox(Debug::Category::RESOURCE_IO);

  groupHeader("Physics & Simulation", ImVec4(1.00f, 0.65f, 0.30f, 1.0f));
  catCheckbox(Debug::Category::PHYSICS);
  catCheckbox(Debug::Category::PHYSICS_SYNC);
  catCheckbox(Debug::Category::PHYSICS_COLLISION);
  catCheckbox(Debug::Category::ANIMATION);
  catCheckbox(Debug::Category::SPAWNING);
  catCheckbox(Debug::Category::TIMELINE);
  catCheckbox(Debug::Category::PARTICLES);
  catCheckbox(Debug::Category::PLANTMANAGER);

  groupHeader("Network", ImVec4(1.00f, 0.45f, 0.75f, 1.0f));
  catCheckbox(Debug::Category::NETWORK);
  catCheckbox(Debug::Category::NETWORK_DISCOVERY);
  catCheckbox(Debug::Category::NETWORK_PACKETS);
  catCheckbox(Debug::Category::NETWORK_OWNERSHIP);
  catCheckbox(Debug::Category::NETWORK_SYNC);
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