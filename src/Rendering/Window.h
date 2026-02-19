#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <stdexcept>

class Window final {
 public:
  Window(int w, int h, const char* title)
      : window(nullptr), width(w), height(h) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!window) {
      throw std::runtime_error("Failed to create GLFW window!");
    }
  }

  ~Window() {
    if (window) {
      glfwDestroyWindow(window);
    }
    glfwTerminate();
  }

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  GLFWwindow* getHandle() const { return window; }

  VkSurfaceKHR createSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create window surface!");
    }
    return surface;
  }

  void setUserPointer(void* ptr) const {
    glfwSetWindowUserPointer(window, ptr);
  }

  void setFramebufferSizeCallback(GLFWframebuffersizefun callback) const {
    glfwSetFramebufferSizeCallback(window, callback);
  }

  void setKeyCallback(GLFWkeyfun callback) const {
    glfwSetKeyCallback(window, callback);
  }

  void setCursorPosCallback(GLFWcursorposfun callback) const {
    glfwSetCursorPosCallback(window, callback);
  }

  void setMouseButtonCallback(GLFWmousebuttonfun callback) const {
    glfwSetMouseButtonCallback(window, callback);
  }

  void setScrollCallback(GLFWscrollfun callback) const {
    glfwSetScrollCallback(window, callback);
  }

  bool shouldClose() const { return glfwWindowShouldClose(window); }

  void pollEvents() const { glfwPollEvents(); }

  void getFramebufferSize(int& w, int& h) const {
    glfwGetFramebufferSize(window, &w, &h);
  }

  void waitEvents() const { glfwWaitEvents(); }

  void setTitle(const char* title) const { glfwSetWindowTitle(window, title); }

 private:
  GLFWwindow* window;
  int width;
  int height;
};