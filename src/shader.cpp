#include "shader.hpp"
#include <cstdint>
#include <fstream>
#include <glad/glad.h>
#include <print>
#include <sstream>
#include <unordered_map>

ShaderProgram::ShaderProgram() { m_program = glCreateProgram(); }

void ShaderProgram::load(const std::filesystem::path &path, ShaderType type) {
  m_path = path;
  std::ifstream shader_file{path};
  std::stringstream shader_source{};
  shader_source << shader_file.rdbuf();
  m_source = shader_source.str();

  uint32_t shader{};

  if (type == ShaderType::Fragment) {
    m_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    shader = static_cast<uint32_t>(m_fragment_shader);
  } else if (type == ShaderType::Vertex) {
    m_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    shader = static_cast<uint32_t>(m_vertex_shader);
  } else {
    std::println(stderr, "Shader type not supported.");
    return;
  }

  const char *source_cstr = m_source.c_str();

  glShaderSource(shader, 1, &source_cstr, nullptr);
  glCompileShader(shader);

  int32_t success{};
  char info_log[512]{};

  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

  if (!success) {
    glGetShaderInfoLog(shader, 512, nullptr, info_log);
    std::println(stderr, "Failed to compile shader '{}' {}", path.c_str(),
                 info_log);
    return;
  }

  glAttachShader(m_program, shader);
  return;
}

void ShaderProgram::link() {

  if (m_vertex_shader == -1 || m_fragment_shader == -1) {
    std::println(stderr,
                 "Expected vertex shader and fragment shader to be loaded.");
  }

  glLinkProgram(m_program);

  int32_t success{};
  char info_log[512]{};

  glGetProgramiv(m_program, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(m_program, 512, nullptr, info_log);
    std::println(stderr, "Failed to link shader '{}' {}", m_path.c_str(),
                 info_log);
    return;
  }

  glDeleteShader(m_fragment_shader);
  glDeleteShader(m_vertex_shader);
}

void ShaderProgram::use() { glUseProgram(m_program); }

void ShaderProgram::set_matrix4x4f(const std::string &name, const float *data) {
  if (m_uniforms.contains(name)) {
    const uint32_t id = m_uniforms.at(name);
    glUniformMatrix4fv(id, 1, GL_FALSE, data);
  } else {
    const uint32_t id = glGetUniformLocation(m_program, name.c_str());
    glUniformMatrix4fv(id, 1, GL_FALSE, data);
    m_uniforms.insert({name, id});
  }
}
