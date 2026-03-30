#pragma once
#include <array>
#include <glm/glm.hpp>

// Axis-aligned bounding sphere frustum culler using Gribb-Hartmann plane extraction.
// Extracts 6 clip planes from a combined view-projection matrix and tests
// spheres against them. A sphere that lies entirely outside any single plane
// is culled; otherwise it is considered potentially visible.

struct Frustum {
  std::array<glm::vec4, 6> planes;  // left, right, bottom, top, near, far
};

// Extract frustum planes from a view-projection matrix.
// GLM uses column-major storage: mat[col][row].
// Row i = (vp[0][i], vp[1][i], vp[2][i], vp[3][i]).
// Uses the Gribb-Hartmann method; planes are normalized so that the w
// component gives the signed distance from the origin.
inline Frustum extractFrustum(const glm::mat4& vp) {
  // Helper: extract row i as vec4
  auto row = [&](int i) -> glm::vec4 {
    return {vp[0][i], vp[1][i], vp[2][i], vp[3][i]};
  };

  const glm::vec4 r0 = row(0);
  const glm::vec4 r1 = row(1);
  const glm::vec4 r2 = row(2);
  const glm::vec4 r3 = row(3);

  Frustum f;
  f.planes[0] = r3 + r0;  // left
  f.planes[1] = r3 - r0;  // right
  f.planes[2] = r3 + r1;  // bottom
  f.planes[3] = r3 - r1;  // top
  // With GLM_FORCE_DEPTH_ZERO_TO_ONE the clip-space z lies in [0, 1],
  // so the near plane is z >= 0 (row2) and far is z <= w (row3 - row2).
  f.planes[4] = r2;        // near  (z >= 0)
  f.planes[5] = r3 - r2;  // far   (z <= w)

  // Normalize so radius comparisons are in world-space units
  for (auto& p : f.planes) {
    const float len = glm::length(glm::vec3(p));
    if (len > 1e-6f) p /= len;
  }

  return f;
}

// Returns true if the sphere (world-space center + radius) is potentially
// visible (intersects or is inside the frustum). Returns false only when the
// sphere is entirely outside at least one plane, guaranteeing no false
// negatives — only false positives near frustum edges.
inline bool sphereInFrustum(const Frustum& f, const glm::vec3& center,
                            float radius) {
  for (const auto& plane : f.planes) {
    if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
  }
  return true;
}
