/* clang-format off */
#include <glad/glad.h>
#include <GLFW/glfw3.h>
/* clang-format on */
#include "buffer_object.hpp"
#include "shader.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <numbers>
#include <print>
#include <utility>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

using Window = std::unique_ptr<GLFWwindow, std::function<void(GLFWwindow *)>>;

namespace fs = std::filesystem;

static float g_camera_yaw{-90.0f};
static float g_camera_pitch{};
double g_mouse_x{}, g_mouse_y{};
uint8_t g_stencil{};
uint8_t g_selected_object{};
uint8_t g_object_id{1};

float g_x_theta{}, g_y_theta{}, g_z_theta{};
float g_x_translation{}, g_y_translation{}, g_z_translation{};

glm::mat4 view{};
glm::mat4 projection{};

struct Mesh {
  int32_t id{g_object_id};
  std::vector<glm::vec3> vertices{};
  std::vector<uint32_t> indices{};
  glm::mat4 model{};
  glm::mat4 position{1.0f};
  glm::mat4 rotation{1.0f};

  void draw() {
    glStencilFunc(GL_ALWAYS, g_object_id, 0xFF);
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
  }
};

Mesh placeholder_mesh{};

Mesh *g_selected_mesh{&placeholder_mesh};

constexpr uint32_t window_width{1280};
constexpr uint32_t window_height{720};

enum class EditorMode : uint8_t { Normal, Rotation, Translation, NumModes };
enum class ManipulationAxis : uint8_t { None, X, Y, Z, NumAxes };

constexpr const char *const editor_mode_str[static_cast<int32_t>(
    EditorMode::NumModes)] = {"Normal", "Rotation", "Translation"};

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
    g_camera_yaw += (curr_mouse_x - g_mouse_x) * 0.1f;
    g_camera_pitch -= (curr_mouse_y - g_mouse_y) * 0.1f;
  }

  if (g_editor_mode == EditorMode::Rotation &&
      g_manipulation_axis == ManipulationAxis::X) {
    float x_theta = glm::radians(static_cast<float>(curr_mouse_y - g_mouse_y));
    g_x_theta += x_theta;
    g_x_theta = std::fmod(g_x_theta, 2 * std::numbers::pi_v<float>);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, x_theta,
                                            glm::vec3{1.0f, 0.0f, 0.0f});
  } else if (g_editor_mode == EditorMode::Rotation &&
             g_manipulation_axis == ManipulationAxis::Y) {
    float y_theta = glm::radians(static_cast<float>(curr_mouse_x - g_mouse_x));
    g_y_theta += y_theta;
    g_y_theta = std::fmod(g_y_theta, 2 * std::numbers::pi_v<float>);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, y_theta,
                                            glm::vec3{0.0f, 1.0f, 0.0f});
  } else if (g_editor_mode == EditorMode::Rotation &&
             g_manipulation_axis == ManipulationAxis::Z) {
    float z_theta = glm::radians(static_cast<float>(curr_mouse_x - g_mouse_x));
    g_z_theta += z_theta;
    g_z_theta = std::fmod(g_z_theta, 2 * std::numbers::pi_v<float>);
    g_selected_mesh->rotation = glm::rotate(g_selected_mesh->rotation, z_theta,
                                            glm::vec3{0.0f, 0.0f, 1.0f});
  }

  if (g_editor_mode == EditorMode::Translation &&
      g_manipulation_axis == ManipulationAxis::X) {
    g_x_translation += static_cast<float>(curr_mouse_x - g_mouse_x) / 250.0f;
    g_selected_mesh->position = glm::translate(
        g_selected_mesh->position,
        glm::vec3{static_cast<float>(curr_mouse_x - g_mouse_x) / 250.0f, 0.0f,
                  0.0f});
  } else if (g_editor_mode == EditorMode::Translation &&
             g_manipulation_axis == ManipulationAxis::Y) {
    g_y_translation += static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f;
    g_selected_mesh->position = glm::translate(
        g_selected_mesh->position,
        glm::vec3{0.0f, static_cast<float>(g_mouse_y - curr_mouse_y) / 250.0f,
                  0.0f});
  } else if (g_editor_mode == EditorMode::Translation &&
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

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    g_selected_object = g_stencil;
  }
}

Mesh parse_objfile(const fs::path &path) {
  std::ifstream objfile{path};
  std::string line{};
  while (std::getline(objfile, line)) {
    std::println("{}", line);
  }
  return {};
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
  basic_shader.load("shaders/shader.vert", ShaderType::Vertex);
  basic_shader.load("shaders/shader.frag", ShaderType::Fragment);
  basic_shader.link();

  ShaderProgram wireframe_shader{};
  wireframe_shader.load("shaders/wireframe.vert", ShaderType::Vertex);
  wireframe_shader.load("shaders/wireframe.frag", ShaderType::Fragment);
  wireframe_shader.link();

  ShaderProgram selection_shader{};
  selection_shader.load("shaders/selection.vert", ShaderType::Vertex);
  selection_shader.load("shaders/selection.frag", ShaderType::Fragment);
  selection_shader.link();

  Mesh cube{};
  cube.model = glm::mat4{1.0f};

  /* clang-format off */
  cube.vertices = {
		// front
		glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0.7f, 0.7f, 0.7f), 
		glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
		glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
		glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.7f, 0.7f, 0.7f),

		// back
		glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
		glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
		glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
		glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0.7f, 0.7f, 0.7f),
  };

	cube.indices = {
		// front
		0, 1, 2,
		2, 3, 0,

		// back
		4, 5, 6,
		6, 7, 4,

		// right
		3, 2, 6,
		6, 7, 3,

		// left
		4, 5, 1,
		1, 0, 4,

		// top
		4, 0, 3,
		3, 7, 4,

		// bottom
		5, 1, 2,
		2, 6, 5
	};
  /* clang-format on */

  uint32_t vao{};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  BufferObject<glm::vec3> vbo{};
  vbo.bind(BufferObjectType::Array, cube.vertices);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec3),
                        (void *)(0));
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec3),
                        (void *)(1 * sizeof(glm::vec3)));

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  BufferObject<uint32_t> ebo{};
  ebo.bind(BufferObjectType::Element, cube.indices);

  glm::vec3 camera_pos{0.0f, 0.0f, 3.0f};
  glm::vec3 camera_target{0.0f, 0.0f, 0.0f};
  glm::vec3 camera_direction = glm::normalize(camera_pos - camera_target);

  glm::vec3 world_up = {0.0f, 1.0f, 0.0f};
  glm::vec3 camera_right =
      glm::normalize(glm::cross(world_up, camera_direction));
  glm::vec3 camera_up = glm::cross(camera_direction, camera_right);
  glm::vec3 camera_front{0.0f, 0.0f, -1.0f};
  glm::vec3 direction{};

  projection = glm::perspective(glm::radians(45.0f),
                                static_cast<float>(window_width) /
                                    static_cast<float>(window_height),
                                0.1f, 100.0f);

  glfwSetCursorPosCallback(window.get(), mouse_move_callback);
  glfwSetMouseButtonCallback(window.get(), mouse_button_callback);
  ImGui_ImplOpenGL3_Init();
  ImGui_ImplGlfw_InitForOpenGL(window.get(), true);

  ImGuiIO &io = ImGui::GetIO();
  ImFont *regular_font = io.Fonts->AddFontFromFileTTF(
      "/home/arvk/.local/share/fonts/CommitMono/CommitMono-400-Regular.otf");
  ImFont *bold_font = io.Fonts->AddFontFromFileTTF(
      "/home/arvk/.local/share/fonts/CommitMono/CommitMono-700-Regular.otf");

  parse_objfile("../cube.obj");

  while (!glfwWindowShouldClose(window.get())) {
    std::string mode_info{};
    if (g_editor_mode != EditorMode::Normal) {
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

    glBindVertexArray(vao);

    view = glm::lookAt(camera_pos, camera_pos + camera_front, camera_up);

    // wireframe
    wireframe_shader.use();
    wireframe_shader.set_matrix4x4f("model", glm::value_ptr(cube.model));
    wireframe_shader.set_matrix4x4f("view", glm::value_ptr(view));
    wireframe_shader.set_matrix4x4f("projection", glm::value_ptr(projection));
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    cube.draw();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (g_selected_object == cube.id) {
      g_selected_mesh = &cube;
      selection_shader.use();
      selection_shader.set_matrix4x4f("model", glm::value_ptr(cube.model));
      selection_shader.set_matrix4x4f("view", glm::value_ptr(view));
      selection_shader.set_matrix4x4f("projection", glm::value_ptr(projection));
      cube.draw();
    } else {
      basic_shader.use();
      basic_shader.set_matrix4x4f("model", glm::value_ptr(cube.model));
      basic_shader.set_matrix4x4f("view", glm::value_ptr(view));
      basic_shader.set_matrix4x4f("projection", glm::value_ptr(projection));
      cube.draw();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::PopFont();

    glfwSwapBuffers(window.get());
    glfwPollEvents();

    glm::vec3 direction{};
    direction.x = std::cosf(glm::radians(g_camera_yaw)) *
                  std::cosf(glm::radians(g_camera_pitch));
    direction.z = std::sinf(glm::radians(g_camera_yaw)) *
                  std::cosf(glm::radians(g_camera_pitch));
    direction.y = std::sinf(glm::radians(g_camera_pitch));

    camera_front = glm::normalize(direction);

    // process input
    if (glfwGetKey(window.get(), GLFW_KEY_W) == GLFW_PRESS) {
      camera_pos += camera_front * 0.1f;
    } else if (glfwGetKey(window.get(), GLFW_KEY_S) == GLFW_PRESS) {
      camera_pos -= camera_front * 0.1f;
    }

    if (glfwGetKey(window.get(), GLFW_KEY_A) == GLFW_PRESS) {
      camera_pos -= glm::normalize(glm::cross(camera_front, camera_up)) * 0.05f;
    } else if (glfwGetKey(window.get(), GLFW_KEY_D) == GLFW_PRESS) {
      camera_pos += glm::normalize(glm::cross(camera_front, camera_up)) * 0.05f;
    }

    if (g_editor_mode == EditorMode::Normal) {
      g_x_theta = 0.0f;
      g_y_theta = 0.0f;
      g_z_theta = 0.0f;

      g_x_translation = 0.0f;
      g_y_translation = 0.0f;
      g_z_translation = 0.0f;
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
  }

  if (g_selected_object == 0) {
    g_selected_mesh = &placeholder_mesh;
  }

  glfwTerminate();

  return 0;
}
