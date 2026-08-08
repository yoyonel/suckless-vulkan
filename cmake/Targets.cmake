# Target declarations and test registration.

function(configure_project_target target_name)
  target_include_directories(${target_name} PRIVATE ${PROJECT_INCLUDE_DIR})
  target_include_directories(${target_name} SYSTEM PRIVATE ${PROJECT_EXT_DIR})
  target_link_libraries(${target_name} PRIVATE ${PROJECT_LINK_LIBS})
endfunction()

add_library(vulkan_rhi SHARED ${RHI_VULKAN_SOURCES})
configure_project_target(vulkan_rhi)
target_link_libraries(vulkan_rhi PRIVATE dl)

add_library(null_rhi SHARED ${RHI_NULL_SOURCES})
configure_project_target(null_rhi)
target_link_libraries(null_rhi PRIVATE dl)

add_executable(vulkan_app ${VULKAN_APP_SOURCES})
configure_project_target(vulkan_app)
target_link_libraries(vulkan_app PRIVATE vulkan_rhi null_rhi)

enable_testing()

add_executable(unit_tests ${UNIT_TEST_SOURCES})
configure_project_target(unit_tests)
target_link_libraries(unit_tests PRIVATE vulkan_rhi null_rhi)

add_test(NAME EngineIntegrationTest
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_test_vulkan.sh
                 $<TARGET_FILE:unit_tests>)
set_tests_properties(EngineIntegrationTest PROPERTIES WORKING_DIRECTORY
                                                      ${CMAKE_SOURCE_DIR})

add_executable(logic_tests ${LOGIC_TEST_SOURCES})
target_include_directories(logic_tests PRIVATE ${PROJECT_INCLUDE_DIR})
target_include_directories(logic_tests SYSTEM PRIVATE ${PROJECT_EXT_DIR})
target_link_libraries(logic_tests PRIVATE glfw glm::glm)

add_test(NAME LogicTests COMMAND $<TARGET_FILE:logic_tests>)
set_tests_properties(LogicTests PROPERTIES WORKING_DIRECTORY
                                           ${CMAKE_SOURCE_DIR})
