#include "buffer_object.hpp"
#include "camera.hpp"
#include "shader.hpp"

#include <GLFW/glfw3.h>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <memory>
#include <numbers>
#include <print>
#include <stack>
#include <utility>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "nlohmann/json.hpp"

using Window = std::unique_ptr<GLFWwindow, std::function<void(GLFWwindow *)>>;

namespace fs = std::filesystem;

double g_mouse_x{}, g_mouse_y{};
uint8_t g_stencil{};
uint8_t g_selected_object{};
uint8_t g_object_id{1};

float g_x_theta{}, g_y_theta{}, g_z_theta{};
float g_x_translation{}, g_y_translation{}, g_z_translation{};

glm::mat4 view{};
glm::mat4 projection{};

Camera camera{};

std::vector<glm::vec3> parse_objfile(const fs::path &path) {
  std::vector<glm::vec3> loaded_vertices{};
  auto split = [](const std::string &s,
                  const char d) -> std::vector<std::string> {
    std::vector<std::string> res{};
    std::string tmp{};
    size_t idx{};
    while (idx < s.size()) {
      if (d == ' ') {
        if (std::isspace(s.at(idx))) {
          res.push_back(tmp);
          tmp.clear();
        } else {
          tmp.push_back(s.at(idx));
        }
      } else {
        if (s.at(idx) == d) {
          res.push_back(tmp);
          tmp.clear();
        } else {
          tmp.push_back(s.at(idx));
        }
      }
      idx++;
    }

    res.push_back(tmp);

    return res;
  };

  std::ifstream objfile{path};
  std::string line{};
  if (!fs::exists(path)) {
    std::println(stderr, "File '{}' does not exist.", path.c_str());
  }

  std::vector<uint32_t> indices{};
  std::vector<glm::vec3> vertices{};
  while (std::getline(objfile, line)) {
    const auto tokens = split(line, ' ');
    const std::string type = tokens.at(0);
    float x{}, y{}, z{}, w{1.0f};
    if (type == "v") {
      if (tokens.size() < 4) {
        std::println(stderr, "Not enough coordinates for vertex.");
      }
      std::from_chars(tokens.at(1).data(),
                      tokens.at(1).data() + tokens.at(1).size(), x);
      std::from_chars(tokens.at(2).data(),
                      tokens.at(2).data() + tokens.at(2).size(), y);
      std::from_chars(tokens.at(3).data(),
                      tokens.at(3).data() + tokens.at(3).size(), z);
      vertices.emplace_back(x, y, z);
    } else if (type == "f") {
      if (tokens.size() < 2) {
        std::println(stderr, "Not enough indices for face.");
      }
      for (const auto &token : tokens) {
        if (token == "f") {
          continue;
        } else {
          if (token.find('/') != std::string::npos) {
            uint32_t idx{};
            auto face_data = split(token, '/');
            std::from_chars(face_data.at(0).data(),
                            face_data.at(0).data() + face_data.at(0).size(),
                            idx);
            indices.push_back(idx - 1);
          }
        }
      }
    }
  }

  if (indices.size() % 3 != 0) {
    std::println(stderr, "Not enough vertices.");
  } else {
    for (size_t i{}; i < indices.size(); i++) {
      glm::vec3 v = vertices.at(indices.at(i));
      loaded_vertices.push_back(v);
    }
  }
  return loaded_vertices;
}

struct Mesh {
  uint32_t id{};
  std::vector<glm::vec3> vertices{};
  std::vector<uint32_t> indices{};
  glm::mat4 model{1.0f};
  glm::mat4 position{1.0f};
  glm::mat4 rotation{1.0f};

  uint32_t vao{};
  BufferObject<glm::vec3> vbo{};

  void load_from_objfile(const fs::path &path) {
    id = g_object_id++;
    vertices = parse_objfile(path);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    vbo.bind(BufferObjectType::Array, vertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 1 * sizeof(glm::vec3),
                          (void *)(0));
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
  }

  void draw(ShaderProgram &shader) {
    if (id == 0) {
      std::println(stderr, "Mesh id must be non-zero.");
      return;
    }
    glBindVertexArray(vao);
    glStencilFunc(GL_ALWAYS, id, 0xFF);
    shader.use();
    shader.set_matrix4x4f("model", glm::value_ptr(model));
    if (indices.empty()) {
      glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    } else {
      glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
  }
};

struct UndoData {
  uint32_t mesh_id{};
  glm::mat4 position{};
  glm::mat4 rotation{};
};

std::stack<UndoData> g_undo_stack{};
bool g_change_recorded{};
bool g_change_undone{};

Mesh placeholder_mesh{};
Mesh *g_selected_mesh{&placeholder_mesh};
std::vector<Mesh *> g_meshes{};

constexpr uint32_t window_width{1280};
constexpr uint32_t window_height{720};

enum class EditorMode : uint8_t {
  Normal,
  Undo,
  Rotation,
  Translation,
  NumModes
};
enum class ManipulationAxis : uint8_t { None, X, Y, Z, NumAxes };

constexpr const char *const editor_mode_str[static_cast<int32_t>(
    EditorMode::NumModes)] = {"Normal", "Undo", "Rotation", "Translation"};

constexpr const char *const manipulation_axis_str[static_cast<int32_t>(
    ManipulationAxis::NumAxes)] = {"??", "X", "Y", "Z"};

EditorMode g_editor_mode{};
ManipulationAxis g_manipulation_axis{};

constexpr inline float rgb_normalized(uint32_t x) {
  return static_cast<float>(x) / 255.0f;
}

void framebuffer_size_callback(GLFWwindow *window, int32_t width,
                               int32_t height) {
  glViewport(0, 0, width, height);
}

void mouse_move_callback(GLFWwindow *window, double curr_mouse_x,
                         double curr_mouse_y) {
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
    camera.xtheta += (curr_mouse_x - g_mouse_x) * 0.1f;
    camera.ytheta -= (g_mouse_y - curr_mouse_y) * 0.1f;
  }

  if (g_editor_mode == EditorMode::Rotation && g_selected_object > 0 &&
      g_manipulation_axis == ManipulationAxis::X) {
    float x_theta = glm::radians(static_cast<float>(curr_mouse_y - g_mouse_y));
    g_x_theta += glm::degrees(x_theta);
    g_x_theta = std::fmod(g_x_theta, 360.0f);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, x_theta,
                                            glm::vec3{1.0f, 0.0f, 0.0f});
  } else if (g_editor_mode == EditorMode::Rotation && g_selected_object > 0 &&
             g_manipulation_axis == ManipulationAxis::Y) {
    float y_theta = glm::radians(static_cast<float>(curr_mouse_x - g_mouse_x));
    g_y_theta += glm::degrees(y_theta);
    g_y_theta = std::fmod(g_y_theta, 360.0f);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, y_theta,
                                            glm::vec3{0.0f, 1.0f, 0.0f});
  } else if (g_editor_mode == EditorMode::Rotation && g_selected_object > 0 &&
             g_manipulation_axis == ManipulationAxis::Z) {
    float z_theta = glm::radians(static_cast<float>(curr_mouse_x - g_mouse_x));
    g_z_theta += glm::degrees(z_theta);
    g_z_theta = std::fmod(g_z_theta, 360.0f);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, z_theta,
                                            glm::vec3{0.0f, 0.0f, 1.0f});
  }

  if (g_editor_mode == EditorMode::Translation && g_selected_object > 0 &&
      g_manipulation_axis == ManipulationAxis::X) {
    g_x_translation += static_cast<float>(curr_mouse_x - g_mouse_x) / 250.0f;
    g_selected_mesh->position = glm::translate(
        g_selected_mesh->position,
        glm::vec3{static_cast<float>(curr_mouse_x - g_mouse_x) / 250.0f, 0.0f,
                  0.0f});
  } else if (g_editor_mode == EditorMode::Translation &&
             g_selected_object > 0 &&
             g_manipulation_axis == ManipulationAxis::Y) {
    g_y_translation += static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f;
    g_selected_mesh->position = glm::translate(
        g_selected_mesh->position,
        glm::vec3{0.0f, static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f,
                  0.0f});
  } else if (g_editor_mode == EditorMode::Translation &&
             g_selected_object > 0 &&
             g_manipulation_axis == ManipulationAxis::Z) {
    g_z_translation += static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f;
    g_selected_mesh->position = glm::translate(
        g_selected_mesh->position,
        glm::vec3{0.0f, 0.0f,
                  static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f});
  }

  g_selected_mesh->model =
      g_selected_mesh->position * g_selected_mesh->rotation;

  g_mouse_x = curr_mouse_x;
  g_mouse_y = curr_mouse_y;

  std::array<uint8_t, 4> color{};
  float depth{};

  glReadPixels(curr_mouse_x, window_height - curr_mouse_y - 1, 1, 1, GL_RGBA,
               GL_UNSIGNED_BYTE, color.data());
  glReadPixels(curr_mouse_x, window_height - curr_mouse_y - 1, 1, 1,
               GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
  glReadPixels(curr_mouse_x, window_height - curr_mouse_y - 1, 1, 1,
               GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &g_stencil);

  float n_mouse_x = (g_mouse_x - (window_width / 2)) / (window_width / 2);
  float n_mouse_y = ((window_height / 2) - g_mouse_y) / (window_height);
}

void mouse_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  camera.distance = ((camera.distance - yoffset) > 0.3f)
                        ? (camera.distance - yoffset)
                        : (camera.distance);
}

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    g_selected_object = g_stencil;
  }
}

std::string format_rotation(char axis, float theta) {
  std::stringstream fmtstream{};
  axis = static_cast<char>(std::toupper(axis));
  constexpr auto w{12};
  fmtstream << "[[";
  if (axis == 'Z') {
    fmtstream << std::left << std::setw(w) << std::format("cos({:.2f})", theta)
              << std::setw(w) << std::format("-sin({:.2f})", theta)
              << std::setw(w) << "0]" << '\n';
    fmtstream << std::left << " [" << std::setw(w)
              << std::format("sin({:.2f})", theta) << std::setw(w)
              << std::format("cos({:.2f})", theta) << std::setw(7) << "0]"
              << '\n';
    fmtstream << std::left << std::setw(w + 1) << " [0" << std::setw(w + 1)
              << "0" << std::setw(w + 1) << "1]]" << '\n';
  } else if (axis == 'Y') {
    fmtstream << std::left << std::setw(w) << std::format("cos({:.2f})", theta)
              << std::setw(w - 5) << '0' << std::setw(w)
              << std::format("-sin({:.2f})]", theta) << '\n';

    fmtstream << std::left << std::setw(w + 1) << " [0" << std::setw(w + 5)
              << "1" << std::setw(w) << "0]" << '\n';

    fmtstream << std::left << " [" << std::setw(w)
              << std::format("-sin({:.2f})", theta) << std::setw(w - 4) << '0'
              << std::format("cos({:.2f})]]", theta) << '\n';

  } else if (axis == 'X') {
    fmtstream << std::left << std::setw(w + 1) << "1" << std::setw(w + 4) << "0"
              << std::setw(w + 1) << "0]" << '\n';

    fmtstream << std::left << std::setw(w) << " [0" << std::setw(w + 1)
              << std::setw(w - 2) << std::format("cos({:.2f})", theta)
              << std::setw(w - 2) << std::format("-sin({:.2f})]", theta)
              << '\n';

    fmtstream << std::left << " [" << std::setw(w)
              << std::format("-sin({:.2f})", theta) << std::setw(w - 3) << '0'
              << std::setw(w) << std::format("cos({:.2f})]]", theta) << '\n';
  }

  return fmtstream.str();
}

std::string format_translation() {
  std::stringstream fmtstream{};
  fmtstream << "[[";
  fmtstream << std::format("{:.2f}]\n", g_x_translation);
  fmtstream << std::format(" [{:.2f}]\n", g_y_translation);
  fmtstream << std::format(" [{:.2f}]]\n", g_z_translation);
  return fmtstream.str();
}

int main() {

  std::ifstream object_states_in("object_states.json");
  nlohmann::json matrix_data{};
  object_states_in >> matrix_data;

  if (!glfwInit()) {
    std::println(stderr, "Failed to initialize GLFW.");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  Window window{
      glfwCreateWindow(window_width, window_height, "Kinematics", NULL, NULL),
      glfwDestroyWindow};

  if (!window) {
    std::println(stderr, "Failed to open GLFW window.");
    return 1;
  }

  glfwMakeContextCurrent(window.get());
  glfwSetFramebufferSizeCallback(window.get(), framebuffer_size_callback);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::println(stderr, "Failed to initialize GLAD.");
    return 1;
  }

  glViewport(0, 0, window_width, window_height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  glLineWidth(3.0f);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ShaderProgram basic_shader{};
  basic_shader.load("shaders/basic.vert", ShaderType::Vertex);
  basic_shader.load("shaders/basic.frag", ShaderType::Fragment);
  basic_shader.link();

  ShaderProgram wireframe_shader{};
  wireframe_shader.load("shaders/wireframe.vert", ShaderType::Vertex);
  wireframe_shader.load("shaders/wireframe.frag", ShaderType::Fragment);
  wireframe_shader.link();

  ShaderProgram selection_shader{};
  selection_shader.load("shaders/selection.vert", ShaderType::Vertex);
  selection_shader.load("shaders/selection.frag", ShaderType::Fragment);
  selection_shader.link();

  // TODO: Convert to filesystem path glob and use std::vector to handle meshes.
  Mesh endeffector{};
  endeffector.load_from_objfile("./RoboticArm/endeffector.obj");
  g_meshes.push_back(&endeffector);

  Mesh base{};
  base.load_from_objfile("./RoboticArm/base.obj");
  g_meshes.push_back(&base);

  Mesh segment1{};
  segment1.load_from_objfile("./RoboticArm/segment1.obj");
  g_meshes.push_back(&segment1);

  Mesh segment2{};
  segment2.load_from_objfile("./RoboticArm/segment2.obj");
  g_meshes.push_back(&segment2);

  Mesh segment3{};
  segment3.load_from_objfile("./RoboticArm/segment3.obj");
  g_meshes.push_back(&segment3);

  Mesh cylinder1{};
  cylinder1.load_from_objfile("./RoboticArm/cylinder.obj");
  g_meshes.push_back(&cylinder1);

  Mesh cylinder2{};
  cylinder2.load_from_objfile("./RoboticArm/cylinder.obj");
  g_meshes.push_back(&cylinder2);

  Mesh cylinder3{};
  cylinder3.load_from_objfile("./RoboticArm/cylinder.obj");
  g_meshes.push_back(&cylinder3);

  Mesh cylinder4{};
  cylinder4.load_from_objfile("./RoboticArm/cylinder.obj");
  g_meshes.push_back(&cylinder4);

  std::vector<float> flattened_matrix{};

  matrix_data["endeffector"]["position"].get_to(flattened_matrix);
  endeffector.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["endeffector"]["rotation"].get_to(flattened_matrix);
  endeffector.rotation = glm::make_mat4(flattened_matrix.data());
  endeffector.model = endeffector.position * endeffector.rotation;

  matrix_data["base"]["position"].get_to(flattened_matrix);
  base.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["base"]["rotation"].get_to(flattened_matrix);
  base.rotation = glm::make_mat4(flattened_matrix.data());
  base.model = base.position * base.rotation;

  matrix_data["segment1"]["position"].get_to(flattened_matrix);
  segment1.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["segment1"]["rotation"].get_to(flattened_matrix);
  segment1.rotation = glm::make_mat4(flattened_matrix.data());
  segment1.model = segment1.position * segment1.rotation;

  matrix_data["segment2"]["position"].get_to(flattened_matrix);
  segment2.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["segment2"]["rotation"].get_to(flattened_matrix);
  segment2.rotation = glm::make_mat4(flattened_matrix.data());
  segment2.model = segment2.position * segment2.rotation;

  matrix_data["segment3"]["position"].get_to(flattened_matrix);
  segment3.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["segment3"]["rotation"].get_to(flattened_matrix);
  segment3.rotation = glm::make_mat4(flattened_matrix.data());
  segment3.model = segment3.position * segment3.rotation;

  matrix_data["cylinder1"]["position"].get_to(flattened_matrix);
  cylinder1.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["cylinder1"]["rotation"].get_to(flattened_matrix);
  cylinder1.rotation = glm::make_mat4(flattened_matrix.data());
  cylinder1.model = cylinder1.position * cylinder1.rotation;

  matrix_data["cylinder2"]["position"].get_to(flattened_matrix);
  cylinder2.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["cylinder2"]["rotation"].get_to(flattened_matrix);
  cylinder2.rotation = glm::make_mat4(flattened_matrix.data());
  cylinder2.model = cylinder2.position * cylinder2.rotation;

  matrix_data["cylinder3"]["position"].get_to(flattened_matrix);
  cylinder3.position = glm::make_mat4(flattened_matrix.data());
  matrix_data["cylinder3"]["rotation"].get_to(flattened_matrix);
  cylinder3.rotation = glm::make_mat4(flattened_matrix.data());
  cylinder3.model = cylinder3.position * cylinder3.rotation;

  projection = glm::perspective(glm::radians(45.0f),
                                static_cast<float>(window_width) /
                                    static_cast<float>(window_height),
                                0.1f, 100.0f);

  glfwSetCursorPosCallback(window.get(), mouse_move_callback);
  glfwSetMouseButtonCallback(window.get(), mouse_button_callback);
  glfwSetScrollCallback(window.get(), mouse_scroll_callback);
  ImGui_ImplOpenGL3_Init();
  ImGui_ImplGlfw_InitForOpenGL(window.get(), true);

  ImGuiIO &io = ImGui::GetIO();
  ImFont *regular_font =
      io.Fonts->AddFontFromFileTTF("./fonts/CommitMono-400-Regular.otf");
  ImFont *bold_font =
      io.Fonts->AddFontFromFileTTF("./fonts/CommitMono-400-Regular.otf");

  while (!glfwWindowShouldClose(window.get())) {
    std::string mode_info{};
    if (g_editor_mode != EditorMode::Normal &&
        g_editor_mode != EditorMode::Undo) {
      mode_info = std::format(
          "Mode: {} Along {} Axis",
          editor_mode_str[static_cast<uint32_t>(g_editor_mode)],
          manipulation_axis_str[static_cast<uint32_t>(g_manipulation_axis)]);
    } else {
      mode_info = std::format(
          "Mode: {}", editor_mode_str[static_cast<uint32_t>(g_editor_mode)]);
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::PushFont(regular_font, 0.0f);
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2{400, 100}, ImGuiCond_Once);
    ImGui::Begin("Editor");
    ImGui::Text(mode_info.c_str());
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2{400, 500}, ImGuiCond_Once);
    ImGui::Begin("Forward Kinematics");
    ImGui::PushFont(bold_font, 0.0f);
    ImGui::Text("X Axis");
    ImGui::PopFont();
    ImGui::Text(format_rotation('x', g_x_theta).c_str());
    ImGui::Text("\n");
    ImGui::PushFont(bold_font, 0.0f);
    ImGui::Text("Y Axis");
    ImGui::PopFont();
    ImGui::Text(format_rotation('y', g_y_theta).c_str());
    ImGui::Text("\n");
    ImGui::PushFont(bold_font, 0.0f);
    ImGui::Text("Z Axis");
    ImGui::PopFont();
    ImGui::Text(format_rotation('z', g_z_theta).c_str());
    ImGui::Text("\n");
    ImGui::PushFont(bold_font, 0.0f);
    ImGui::Text("Translation");
    ImGui::PopFont();
    ImGui::Text(format_translation().c_str());
    ImGui::End();

    glClearColor(rgb_normalized(0x1a), rgb_normalized(0x1a),
                 rgb_normalized(0x1a), rgb_normalized(0xff));

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    view = glm::translate(camera.view(), camera.offset);

    // wireframe
    wireframe_shader.use();
    wireframe_shader.set_matrix4x4f("view", glm::value_ptr(view));
    wireframe_shader.set_matrix4x4f("projection", glm::value_ptr(projection));
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (size_t i{}; i < g_meshes.size(); ++i) {
      g_meshes.at(i)->draw(wireframe_shader);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    selection_shader.use();
    selection_shader.set_matrix4x4f("view", glm::value_ptr(view));
    selection_shader.set_matrix4x4f("projection", glm::value_ptr(projection));

    basic_shader.use();
    basic_shader.set_matrix4x4f("view", glm::value_ptr(view));
    basic_shader.set_matrix4x4f("projection", glm::value_ptr(projection));

    for (size_t i{}; i < g_meshes.size(); ++i) {
      if (g_selected_object == g_meshes.at(i)->id) {
        g_selected_mesh = g_meshes.at(i);
        g_meshes.at(i)->draw(selection_shader);
      } else {
        g_meshes.at(i)->draw(basic_shader);
      }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::PopFont();

    glfwSwapBuffers(window.get());
    glfwPollEvents();

    // process input
    if (glfwGetKey(window.get(), GLFW_KEY_W) == GLFW_PRESS) {
      camera.offset.y -= 0.1f;
    } else if (glfwGetKey(window.get(), GLFW_KEY_S) == GLFW_PRESS) {
      camera.offset.y += 0.1f;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_A) == GLFW_PRESS) {
      camera.offset +=
          glm::normalize(glm::cross(camera.up, camera.front())) * 0.1f;
    } else if (glfwGetKey(window.get(), GLFW_KEY_D) == GLFW_PRESS) {
      camera.offset -=
          glm::normalize(glm::cross(camera.up, camera.front())) * 0.1f;
    }

    if ((g_editor_mode != EditorMode::Normal) &&
        (g_editor_mode != EditorMode::Undo) && !g_change_recorded) {
      if (g_selected_object > 0) {
        g_undo_stack.push({g_selected_object, g_selected_mesh->position,
                           g_selected_mesh->rotation});
      }
      g_change_recorded = true;
    }
    if (g_editor_mode == EditorMode::Normal) {
      g_x_theta = 0.0f;
      g_y_theta = 0.0f;
      g_z_theta = 0.0f;

      g_x_translation = 0.0f;
      g_y_translation = 0.0f;
      g_z_translation = 0.0f;

      g_change_recorded = false;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_Q) == GLFW_PRESS) {
      break;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_R) == GLFW_PRESS &&
        g_editor_mode == EditorMode::Normal) {
      g_editor_mode = EditorMode::Rotation;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_T) == GLFW_PRESS &&
        g_editor_mode == EditorMode::Normal) {
      g_editor_mode = EditorMode::Translation;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS &&
        g_editor_mode != EditorMode::Normal) {
      g_manipulation_axis = ManipulationAxis::None;
      g_editor_mode = EditorMode::Normal;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_X) == GLFW_PRESS &&
        g_editor_mode != EditorMode::Normal) {
      g_manipulation_axis = ManipulationAxis::X;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_Y) == GLFW_PRESS &&
        g_editor_mode != EditorMode::Normal) {
      g_manipulation_axis = ManipulationAxis::Y;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_Z) == GLFW_PRESS &&
        g_editor_mode != EditorMode::Normal) {
      g_manipulation_axis = ManipulationAxis::Z;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_U) == GLFW_PRESS &&
        g_editor_mode == EditorMode::Normal) {
      g_editor_mode = EditorMode::Undo;
    }

    if (g_editor_mode == EditorMode::Undo && !g_change_undone) {
      if (!g_undo_stack.empty()) {
        UndoData undo = g_undo_stack.top();
        g_undo_stack.pop();
        g_change_undone = true;

        for (size_t i{}; i < g_meshes.size(); ++i) {
          if (g_meshes.at(i)->id == undo.mesh_id) {
            std::println("Undo performed for mesh {}", undo.mesh_id);
            g_meshes.at(i)->position = undo.position;
            g_meshes.at(i)->rotation = undo.rotation;
            g_meshes.at(i)->model =
                g_meshes.at(i)->position * g_meshes.at(i)->rotation;
            break;
          }
        }
      }
    }

    if ((glfwGetKey(window.get(), GLFW_KEY_U) == GLFW_RELEASE) &&
        g_change_undone && (g_editor_mode == EditorMode::Undo)) {
      g_editor_mode = EditorMode::Normal;
      g_change_undone = false;
    }

    if (g_selected_object == 0) {
      g_selected_mesh = &placeholder_mesh;
    }
  }

  glfwTerminate();

  auto flatten_model_matrix = [](const glm::mat4 &m) -> std::vector<float> {
    return std::vector<float>(glm::value_ptr(m), glm::value_ptr(m) + 16);
  };

  nlohmann::json object_states_data = {
      {"endeffector",
       {
           {"position", flatten_model_matrix(endeffector.position)},
           {"rotation", flatten_model_matrix(endeffector.rotation)},
       }},

      {"base",
       {
           {"position", flatten_model_matrix(base.position)},
           {"rotation", flatten_model_matrix(base.rotation)},
       }},
      {"segment1",
       {
           {"position", flatten_model_matrix(segment1.position)},
           {"rotation", flatten_model_matrix(segment1.rotation)},
       }},
      {"segment2",
       {
           {"position", flatten_model_matrix(segment2.position)},
           {"rotation", flatten_model_matrix(segment2.rotation)},
       }},
      {"segment3",
       {
           {"position", flatten_model_matrix(segment3.position)},
           {"rotation", flatten_model_matrix(segment3.rotation)},
       }},

      {"cylinder1",
       {{"position", flatten_model_matrix(cylinder1.position)},
        {"rotation", flatten_model_matrix(cylinder1.rotation)}}},

      {"cylinder2",
       {{"position", flatten_model_matrix(cylinder2.position)},
        {"rotation", flatten_model_matrix(cylinder2.rotation)}}},

      {"cylinder3",
       {{"position", flatten_model_matrix(cylinder3.position)},
        {"rotation", flatten_model_matrix(cylinder3.rotation)}}},

      {"cylinder4",
       {{"position", flatten_model_matrix(cylinder4.position)},
        {"rotation", flatten_model_matrix(cylinder4.rotation)}}},

  };

  std::ofstream object_states_out("object_states.json");
  object_states_out << object_states_data << std::endl;

  return 0;
}
