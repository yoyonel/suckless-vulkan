set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)
project(VulkanApp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional: Address Sanitizer + Undefined Behavior Sanitizer for memory/safety
# checks. Runtime suppressions via LSAN_OPTIONS=suppressions=./.asan_ignorefile
option(ENABLE_SANITIZERS "Enable ASan + UBSan for memory and safety checks" OFF)
if(ENABLE_SANITIZERS)
  message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined \
-fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
  message(
    STATUS "ASan suppressions: set LSAN_OPTIONS=suppressions=./.asan_ignorefile"
  )
endif()

# Optional: code coverage instrumentation for unit/logical tests (gcovr style).
option(ENABLE_COVERAGE "Enable coverage flags for GCC/Clang (gcovr)" OFF)
if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling code coverage instrumentation (gcovr)")
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
  else()
    message(WARNING "Coverage requested but compiler is not GCC/Clang")
  endif()
endif()

# Optional: LLVM code coverage instrumentation (llvm-cov style, clang required).
option(ENABLE_LLVM_COV "Enable LLVM coverage instrumentation (llvm-cov)" OFF)
if(ENABLE_LLVM_COV)
  message(STATUS "Enabling LLVM code coverage instrumentation")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping -O0 -g)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
# NOUVEAU : On exige GLM
find_package(glm REQUIRED)

add_executable(
  vulkan_app
  src/main.cpp
  src/vk_engine.cpp
  src/vk_engine_frame.cpp
  src/vk_engine_envmap.cpp
  src/vk_engine_runtime.cpp
  src/vk_engine_init.cpp
  src/vk_engine_ibl.cpp
  src/camera.cpp
  src/app_log.cpp
  src/runtime_controls.cpp
  src/stb_image_impl.cpp
  src/stb_image_write_impl.cpp
  src/material_loader.cpp
  ext/vma/vma_impl.cpp
  ext/cjson/cJSON.c)
target_include_directories(vulkan_app PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(vulkan_app SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/ext)
# NOUVEAU : On lie glm::glm
target_link_libraries(vulkan_app Vulkan::Vulkan glfw glm::glm)

enable_testing()
add_executable(
  unit_tests
  tests/test_main.cpp
  src/vk_engine.cpp
  src/vk_engine_frame.cpp
  src/vk_engine_envmap.cpp
  src/vk_engine_runtime.cpp
  src/vk_engine_init.cpp
  src/vk_engine_ibl.cpp
  src/camera.cpp
  src/app_log.cpp
  src/runtime_controls.cpp
  src/stb_image_impl.cpp
  src/stb_image_write_impl.cpp
  src/material_loader.cpp
  ext/vma/vma_impl.cpp
  ext/cjson/cJSON.c)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(unit_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/ext)
# NOUVEAU : On lie glm::glm aux tests aussi
target_link_libraries(unit_tests Vulkan::Vulkan glfw glm::glm)
add_test(NAME EngineIntegrationTest
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_test_vulkan.sh
                 $<TARGET_FILE:unit_tests>)
set_tests_properties(EngineIntegrationTest PROPERTIES WORKING_DIRECTORY
                                                      ${CMAKE_SOURCE_DIR})

add_executable(logic_tests tests/test_logic.cpp src/app_log.cpp
                           src/runtime_controls.cpp src/camera.cpp)
target_include_directories(logic_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(logic_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/ext)
target_link_libraries(logic_tests glfw glm::glm)
add_test(NAME LogicTests COMMAND $<TARGET_FILE:logic_tests>)
set_tests_properties(LogicTests PROPERTIES WORKING_DIRECTORY
                                           ${CMAKE_SOURCE_DIR})

# On coupe les warnings pour le fichier source C++ de VMA
set_source_files_properties(ext/vma/vma_impl.cpp PROPERTIES COMPILE_FLAGS "-w")
