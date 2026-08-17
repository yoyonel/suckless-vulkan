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
set_target_properties(vulkan_app PROPERTIES ENABLE_EXPORTS TRUE)
configure_project_target(vulkan_app)
target_link_libraries(vulkan_app PRIVATE vulkan_rhi null_rhi)
if(TRACY_AVAILABLE)
    target_link_libraries(vulkan_app PRIVATE Tracy::TracyClient)
endif()

enable_testing()

add_executable(unit_tests ${UNIT_TEST_SOURCES})
configure_project_target(unit_tests)
target_link_libraries(unit_tests PRIVATE vulkan_rhi null_rhi)
if(TRACY_AVAILABLE)
    target_link_libraries(unit_tests PRIVATE Tracy::TracyClient)
endif()

add_test(NAME EngineIntegrationTest
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_test_vulkan.sh
                 $<TARGET_FILE:unit_tests>)
set_tests_properties(EngineIntegrationTest PROPERTIES WORKING_DIRECTORY
                                                      ${CMAKE_SOURCE_DIR}
                                                      TIMEOUT 180)

add_test(NAME SmokeTestApp
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/smoke_test_app.sh
                 $<TARGET_FILE:vulkan_app>)
set_tests_properties(SmokeTestApp PROPERTIES WORKING_DIRECTORY
                                             ${CMAKE_SOURCE_DIR}
                                             TIMEOUT 180)

add_executable(logic_tests ${LOGIC_TEST_SOURCES})
target_include_directories(logic_tests PRIVATE ${PROJECT_INCLUDE_DIR})
target_include_directories(logic_tests SYSTEM PRIVATE ${PROJECT_EXT_DIR})
target_link_libraries(logic_tests PRIVATE glfw glm::glm)
if(TRACY_AVAILABLE)
    target_link_libraries(logic_tests PRIVATE Tracy::TracyClient)
endif()

add_test(NAME LogicTests COMMAND $<TARGET_FILE:logic_tests>)
set_tests_properties(LogicTests PROPERTIES WORKING_DIRECTORY
                                           ${CMAKE_SOURCE_DIR})
