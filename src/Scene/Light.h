#pragma once
#include <glm/glm.hpp>
#include <string>
#include <utility>

using LightID = uint32_t;
constexpr LightID INVALID_LIGHT_ID = 0;

enum class LightType { Point, Sun };

class Light final {
 public:
  Light()
      : name("Unnamed Light"),
        position(0.0f),
        direction(0.0f, -1.0f, 0.0f),
        color(1.0f),
        intensity(1.0f),
        constant(1.0f),
        linear(0.09f),
        quadratic(0.032f),
        type(LightType::Point),
        shadowMapIndex(UINT32_MAX),
        castsShadows(true) {}

  Light(const Light&) = default;
  Light& operator=(const Light&) = default;

  void setType(LightType t) { type = t; }
  LightType getType() const { return type; }

  void setPosition(const glm::vec3& p) { position = p; }
  void getPosition(glm::vec3& outPosition) const { outPosition = position; }

  void setDirection(const glm::vec3& d) { direction = d; }
  void getDirection(glm::vec3& outDirection) const { outDirection = direction; }

  void setColor(const glm::vec3& c) { color = c; }
  void getColor(glm::vec3& outColor) const { outColor = color; }

  void setIntensity(float i) { intensity = i; }
  float getIntensity() const { return intensity; }

  void setConstant(float c) { constant = c; }
  float getConstant() const { return constant; }

  void setLinear(float l) { linear = l; }
  float getLinear() const { return linear; }

  void setQuadratic(float q) { quadratic = q; }
  float getQuadratic() const { return quadratic; }

  void setName(const std::string& n) { name = n; }
  void getName(std::string& outName) const { outName = name; }

  void setCastsShadows(bool cast) { castsShadows = cast; }
  bool getCastsShadows() const { return castsShadows; }

  void setShadowMapIndex(uint32_t idx) { shadowMapIndex = idx; }
  uint32_t getShadowMapIndex() const { return shadowMapIndex; }

  void setCutOff(float c) { cutOff = c; }
  float getCutOff() const { return cutOff; }

  void setOuterCutOff(float c) { outerCutOff = c; }
  float getOuterCutOff() const { return outerCutOff; }

  void getEffectivePosition(glm::vec3& outPos) const {
    if (type == LightType::Sun) {
      outPos = -direction * 1000.0f;
    } else {
      outPos = position;
    }
  }

 private:
  std::string name;
  glm::vec3 position;
  glm::vec3 direction;
  glm::vec3 color;
  float intensity;
  float constant;
  float linear;
  float quadratic;
  float cutOff = 0.0f;
  float outerCutOff = 0.0f;
  LightType type;
  uint32_t shadowMapIndex;
  bool castsShadows;
};

class LightBuilder final {
 public:
  LightBuilder() = default;

  LightBuilder& type(LightType t) {
    light.setType(t);
    return *this;
  }

  LightBuilder& name(const std::string& n) {
    light.setName(n);
    return *this;
  }

  LightBuilder& position(const glm::vec3& pos) {
    light.setPosition(pos);
    return *this;
  }

  LightBuilder& position(float x, float y, float z) {
    light.setPosition(glm::vec3(x, y, z));
    return *this;
  }

  LightBuilder& direction(const glm::vec3& dir) {
    light.setDirection(glm::normalize(dir));
    return *this;
  }

  LightBuilder& direction(float x, float y, float z) {
    light.setDirection(glm::normalize(glm::vec3(x, y, z)));
    return *this;
  }

  LightBuilder& color(const glm::vec3& c) {
    light.setColor(c);
    return *this;
  }

  LightBuilder& color(float r, float g, float b) {
    light.setColor(glm::vec3(r, g, b));
    return *this;
  }

  LightBuilder& intensity(float i) {
    light.setIntensity(i);
    return *this;
  }

  LightBuilder& attenuation(float constant, float linear, float quadratic) {
    light.setConstant(constant);
    light.setLinear(linear);
    light.setQuadratic(quadratic);
    return *this;
  }

  LightBuilder& castsShadows(bool shadows) {
    light.setCastsShadows(shadows);
    return *this;
  }

  void build(Light& outLight) const { outLight = light; }

 private:
  Light light;
};