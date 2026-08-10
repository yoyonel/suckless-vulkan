# Third-party dependencies and common target linkage.

include(FetchContent)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

set(PROJECT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src)
set(PROJECT_EXT_DIR ${CMAKE_SOURCE_DIR}/ext)

set(PROJECT_LINK_LIBS
    Vulkan::Vulkan
    glfw
    glm::glm)

if(ENABLE_TRACY)
    message(STATUS "Enabling Tracy profiler integration")
    set(TRACY_ENABLE ON CACHE BOOL "Enable profiling in Tracy" FORCE)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.13.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    FetchContent_MakeAvailable(tracy)
    
    add_compile_definitions(TRACY_ENABLE)
    include_directories(${tracy_SOURCE_DIR}/public)
    set(TRACY_AVAILABLE ON)
endif()
