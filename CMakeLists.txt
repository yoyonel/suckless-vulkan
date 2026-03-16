set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)
project(VulkanApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
# NOUVEAU : On exige GLM
find_package(glm REQUIRED)

add_executable(vulkan_app src/main.cpp src/vk_engine.cpp ext/vma/vma_impl.cpp)
target_include_directories(vulkan_app PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(vulkan_app SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/ext)
# NOUVEAU : On lie glm::glm
target_link_libraries(vulkan_app Vulkan::Vulkan glfw glm::glm)

enable_testing()
add_executable(unit_tests tests/test_main.cpp src/vk_engine.cpp
                          ext/vma/vma_impl.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(unit_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/ext)
# NOUVEAU : On lie glm::glm aux tests aussi
target_link_libraries(unit_tests Vulkan::Vulkan glfw glm::glm)
add_test(NAME EngineIntegrationTest
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_test_vulkan.sh
                 $<TARGET_FILE:unit_tests>)
set_tests_properties(EngineIntegrationTest PROPERTIES WORKING_DIRECTORY
                                                      ${CMAKE_SOURCE_DIR})

# On coupe les warnings pour le fichier source C++ de VMA
set_source_files_properties(ext/vma/vma_impl.cpp PROPERTIES COMPILE_FLAGS "-w")
