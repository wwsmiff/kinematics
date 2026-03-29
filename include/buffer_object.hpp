#pragma once
#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <print>
#include <type_traits>
#include <vector>

enum class BufferObjectType : uint8_t {
  Array,
  Element,
};

template <class T>
concept Number = std::is_integral<T>::value || std::is_floating_point<T>::value;

template <class> struct is_glm_vec : std::false_type {};

template <glm::length_t L, class T, glm::qualifier Q>
struct is_glm_vec<glm::vec<L, T, Q>> : std::true_type {};

template <class T>
concept BufferObjectDataType = is_glm_vec<T>::value || Number<T>;

template <BufferObjectDataType T> class BufferObject {
public:
  void bind(BufferObjectType type, const std::vector<T> &data) {
    m_type = type;
    std::copy(data.begin(), data.end(), std::back_inserter(m_data));

    glGenBuffers(1, &m_id);
    GLenum target{};
    if (m_type == BufferObjectType::Array) {
      target = GL_ARRAY_BUFFER;
    } else if (m_type == BufferObjectType::Element) {
      target = GL_ELEMENT_ARRAY_BUFFER;
    }
    glBindBuffer(target, m_id);
    glBufferData(target, sizeof(T) * m_data.size(), m_data.data(),
                 GL_STATIC_DRAW);
  }

private:
  uint32_t m_id{};
  std::vector<T> m_data{};
  BufferObjectType m_type{};
};
