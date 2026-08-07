# Application and test source layout.

set(APP_SHARED_SOURCES
    src/vk_engine.cpp
    src/vk_engine_frame.cpp
    src/vk_engine_envmap.cpp
    src/vk_engine_runtime.cpp
    src/vk_engine_init.cpp
    src/vk_engine_ibl.cpp
    src/tracy_vulkan.cpp
    src/camera.cpp
    src/app_log.cpp
    src/runtime_controls.cpp
    src/stb_image_impl.cpp
    src/stb_image_write_impl.cpp
    src/material_loader.cpp
    ext/vma/vma_impl.cpp
    ext/cjson/cJSON.c)

set(VULKAN_APP_SOURCES
    src/main.cpp
    src/tracy_client.cpp
    ${APP_SHARED_SOURCES})

set(UNIT_TEST_SOURCES
    tests/test_main.cpp
    ${APP_SHARED_SOURCES})

set(LOGIC_TEST_SOURCES
    tests/test_logic.cpp
    src/app_log.cpp
    src/runtime_controls.cpp
    src/camera.cpp
    src/vk_engine_runtime.cpp)
