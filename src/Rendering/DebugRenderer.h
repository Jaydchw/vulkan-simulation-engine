#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <vulkan/vulkan.h>
#include "vma/vk_mem_alloc.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

class RenderDevice;

// ────────────────────────────────────────────────────────────────────────────
// DebugRenderer
//
// Renders physics debug geometry (collider wireframes, velocity arrows, sleep
// indicators, broadphase grid) as real 3D lines inside the offscreen pass.
//
// Usage per frame:
//   1. Call begin(frameIndex)         ← under simMutex, to reset the buffer
//   2. Call add*(…) helpers           ← still under simMutex
//   3. Call render(cmd, vp, frame, extent) ← inside vkCmdBeginRendering block
// ────────────────────────────────────────────────────────────────────────────
class DebugRenderer {
 public:
  struct Vertex {
    glm::vec3 pos;    // location 0 — VK_FORMAT_R32G32B32_SFLOAT
    glm::vec4 color;  // location 1 — VK_FORMAT_R32G32B32A32_SFLOAT
  };

  // Maximum line-endpoint vertices per frame (32 K lines).
  static constexpr uint32_t MAX_VERTICES = 65536;

  void init(VkDevice device, RenderDevice* renderDevice,
            VkFormat colorFormat, int framesInFlight);
  void cleanup();

  // ── Geometry builders (call under simMutex) ──────────────────────────────
  void begin(uint32_t frameIndex);

  void addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

  // Three great-circle rings oriented by 'orientation'.
  void addWireSphere(const glm::vec3& center, float radius,
                     const glm::quat& orientation, const glm::vec4& color,
                     int segs = 24);

  // Oriented box from half-extents (already scaled).
  void addWireBox(const glm::vec3& center, const glm::vec3& halfExtents,
                  const glm::quat& orientation, const glm::vec4& color);

  // Capsule: two end caps + cylinder side-lines + hemisphere arcs.
  void addWireCapsule(const glm::vec3& center, float radius, float halfHeight,
                      const glm::quat& orientation, const glm::vec4& color,
                      int segs = 16);

  // Flat-capped cylinder.
  void addWireCylinder(const glm::vec3& center, float radius, float halfHeight,
                       const glm::quat& orientation, const glm::vec4& color,
                       int segs = 16);

  // Line with a small arrowhead at 'to'.
  void addArrow(const glm::vec3& from, const glm::vec3& to,
                const glm::vec4& color);

  // ── Render (call from recordCommandBuffer, inside offscreen pass) ─────────
  void render(VkCommandBuffer cmd, const glm::mat4& viewProj,
              uint32_t frameIndex, VkExtent2D extent);

 private:
  VkDevice       device       = VK_NULL_HANDLE;
  RenderDevice*  renderDevice = nullptr;
  VkPipeline     pipeline     = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

  // Per-frame host-visible vertex buffers (persistently mapped).
  std::vector<VkBuffer>      vertexBuffers;
  std::vector<VmaAllocation> vertexAllocs;
  std::vector<Vertex*>       mappedPtrs;
  std::vector<uint32_t>      vertexCounts;

  uint32_t currentFrame = 0;
};
