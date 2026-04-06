#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct Camera {
  float yaw{-90.0f};
  float pitch{};
  float distance{15.0f};
  glm::vec3 target{0.0f, 0.0f, 0.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  glm::vec3 right{-1.0f, 0.0f, 0.0f};
  glm::vec3 offset{};
  float xtheta{};
  float ytheta{};

  glm::mat4 view() {
    glm::vec3 pos = position();
    return glm::lookAt(pos, glm::vec3{}, up);
  }

  glm::vec3 direction() {
    glm::vec3 dir{};
    dir.x = std::cosf(glm::radians(xtheta)) * std::cosf(glm::radians(ytheta));
    dir.z = std::sinf(glm::radians(xtheta)) * std::cosf(glm::radians(ytheta));
    dir.y = std::sinf(glm::radians(ytheta));

    return dir;
  }

  glm::vec3 position() { return distance * direction(); }

  glm::vec3 front() { return glm::normalize(direction()); }
};
