#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

enum class ShaderType { Fragment, Vertex };

class ShaderProgram {
public:
  ShaderProgram();
  void load(const std::filesystem::path &path, ShaderType type);
  void link();
  void use();
  void set_matrix4x4f(const std::string &name, const float *data);

  const uint32_t program_id() const { return m_program; }
  const int32_t vertex_shader_id() const { return m_vertex_shader; }
  const int32_t fragment_shader_id() const { return m_fragment_shader; }

  const std::string &source() const { return m_source; }

private:
  std::unordered_map<std::string, uint32_t> m_uniforms{};
  std::filesystem::path m_path{};
  int32_t m_vertex_shader{-1};
  int32_t m_fragment_shader{-1};
  uint32_t m_program{};
  std::string m_source{};
  ShaderType m_type{};
};
