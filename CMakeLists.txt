cmake_minimum_required(VERSION 4.2)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw
	GIT_TAG 7b6aead
)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm
	GIT_TAG 8d1fd52
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui
	GIT_TAG 6ded523
)

FetchContent_Declare(
	nlohmannjson
	GIT_REPOSITORY https://github.com/nlohmann/json
	GIT_TAG 55f9368
)

FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(glm)
FetchContent_MakeAvailable(imgui)
file(GLOB imgui_sources "${imgui_SOURCE_DIR}/*.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
)
FetchContent_MakeAvailable(nlohmannjson)


project(kinematics)
add_executable(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${glfw_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${glm_SOURCE_DIR}/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${imgui_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE "${nlohmannjson_SOURCE_DIR}/include")
target_link_libraries(${PROJECT_NAME} PRIVATE GL glfw glm)

target_sources(${PROJECT_NAME} PRIVATE
	src/main.cpp
	src/shader.cpp
	src/camera.cpp
	src/glad.c
)

target_sources(${PROJECT_NAME} PRIVATE
	${imgui_sources}
)
