cmake_minimum_required(VERSION 4.2)
project(LiteFXStructuredBufferBug LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This reproducer targets the DirectX 12 backend on Windows.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)
set(BUILD_SHARED_LIBS OFF)
set(LITEFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_DIRECTX_12_BACKEND ON CACHE BOOL "" FORCE)
set(LITEFX_BUILD_VULKAN_BACKEND OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_DEFINE_BUILDERS ON CACHE BOOL "" FORCE)
set(LITEFX_BUILD_WITH_GLM OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_WITH_DIRECTX_MATH OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_WITH_PIX_RUNTIME OFF CACHE BOOL "" FORCE)
set(LITEFX_BUILD_SUPPORT_DEBUG_MARKERS OFF CACHE BOOL "" FORCE)

# Pin the inspected revision so the report remains reproducible.
# FETCHCONTENT_SOURCE_DIR_LITEFX can select a local checkout to test a fix.
FetchContent_Declare(LiteFX
    GIT_REPOSITORY https://github.com/crud89/LiteFX.git
    GIT_TAG 7ed63e973434d90f8dc00be6570e20a7348ec00c
    SOURCE_SUBDIR src
)
FetchContent_MakeAvailable(LiteFX)

add_executable(bug_example main.cpp)
target_compile_definitions(bug_example PRIVATE WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(bug_example PRIVATE /Zc:preprocessor)
endif()
target_link_libraries(bug_example PRIVATE LiteFX::DirectX12 user32)

litefx_deploy_runtime(bug_example)
target_add_shader_program(bug_example
    SOURCE "shaders/probe.hlsl"
    COMPUTE CSMain
    SHADER_MODEL 6_5
    DXIL
)
