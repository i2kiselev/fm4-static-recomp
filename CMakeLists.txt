# Forza Motorsport 4 - native ShiftGlue host

cmake_minimum_required(VERSION 3.25)
project(fm4_static_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL uses native Windows HID and XInput backends here; its optional libusb
# probe should not make a normal Windows build look like it lacks a dependency.
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "Disable optional libusb HID backend on Windows" FORCE)

option(FM4_BUILD_TESTS "Add the fm4_unit_tests target (built only when asked for)" OFF)
option(FM4_TESTS_ONLY "Configure native tests without discs or generated game code" OFF)
if(FM4_TESTS_ONLY)
    set(FM4_BUILD_TESTS ON)
endif()

include(cmake/Fm4RexGlue.cmake)

option(FM4_REQUIRE_TEMPORAL_UPSCALERS "Require the FSR 2/3 provider and staged runtime" OFF)
if(FM4_REQUIRE_TEMPORAL_UPSCALERS AND APPLE)
    if(NOT REXGLUE_FFX_API_INCLUDE_DIR OR NOT REXGLUE_FFX_API_LOADER OR NOT REXGLUE_FIDELITYFX_RUNTIME)
        message(FATAL_ERROR "The Mac launcher requires the FidelityFX API headers, FSR provider and runtime. "
            "Check the SDK configure log for failed header downloads.")
    endif()
endif()

set(FM4_HOST_SOURCES
    src/main.cpp
    src/built_in_defaults.cpp
    src/tool_mode.cpp
    src/disc2_install.cpp
    src/game_hooks.cpp
    src/car_dirt.cpp
    src/code_image_check.cpp
    src/shadow_map.cpp
    src/file_open_log.cpp
    src/game_language.cpp
    src/string_log.cpp
    src/scripted_input.cpp
    src/shader_cache/cache_writer.cpp
    src/shader_cache/disc_shaders.cpp
    src/config/host_config.cpp
    src/input/controller_layout.cpp
    src/input/controls.cpp
    src/input/input_context.cpp
    src/input/keyboard_ramp.cpp
    src/input/keycap.cpp
    src/input/prompt_strings.cpp
    src/input/prompt_swap.cpp
    src/ui/controls_page.cpp
    src/ui/graphics_page.cpp
    src/ui/hostui/glyph_atlas.cpp
    src/ui/hostui/host_ui.cpp
    src/ui/hostui/menu.cpp
    src/ui/hostui/ui_archive.cpp
    src/ui/hostui/ui_fonts.cpp
    src/ui/hostui/vector_font.cpp
    src/ui/hostui/xds_texture.cpp
)

if(NOT FM4_TESTS_ONLY)
if(WIN32)
    add_executable(fm4_static_recomp WIN32
        ${FM4_HOST_SOURCES}
        # Merged into the linker's manifest: the UTF-8 code page.
        src/fm4_static_recomp.manifest
        src/fm4_static_recomp.rc
    )
    fm4_static_recomp_attach_rexglue(fm4_static_recomp)
    target_link_libraries(fm4_static_recomp PRIVATE bcrypt d3d12 dxgi rex::gpu-fh1 renderdoc)
else()
    add_executable(fm4_static_recomp
        ${FM4_HOST_SOURCES}
    )
    fm4_static_recomp_attach_rexglue(fm4_static_recomp)
    target_link_libraries(fm4_static_recomp PRIVATE rex::gpu-fh1)
endif()

endif() # NOT FM4_TESTS_ONLY

if(FM4_BUILD_TESTS)
    if(NOT TARGET Catch2::Catch2WithMain)
        add_subdirectory("${REXSDK_DIR}/thirdparty/catch2" catch2 EXCLUDE_FROM_ALL)
    endif()
    add_executable(fm4_unit_tests EXCLUDE_FROM_ALL
        tests/controls_test.cpp
        tests/keyboard_ramp_test.cpp
        tests/input_context_test.cpp
        tests/keycap_test.cpp
        tests/prompt_swap_test.cpp
        tests/prompt_strings_test.cpp
        tests/car_dirt_test.cpp
        tests/code_image_check_test.cpp
        tests/shadow_map_test.cpp
        tests/file_open_log_test.cpp
        tests/game_language_test.cpp
        tests/string_log_test.cpp
        tests/string_tables_test.cpp
        tests/ui_fonts_test.cpp
        tests/graphics_page_test.cpp
        tests/guest_frame_stats_test.cpp
        tests/controls_page_test.cpp
        tests/controller_layout_test.cpp
        src/car_dirt.cpp
        src/code_image_check.cpp
        src/shadow_map.cpp
        src/file_open_log.cpp
        src/game_language.cpp
        src/string_log.cpp
        src/config/host_config.cpp
        src/input/controller_layout.cpp
        src/input/controls.cpp
        src/input/keyboard_ramp.cpp
        src/input/input_context.cpp
        src/input/keycap.cpp
        src/input/prompt_swap.cpp
        src/input/prompt_strings.cpp
        src/ui/hostui/ui_archive.cpp
        src/ui/hostui/ui_fonts.cpp
        src/ui/hostui/vector_font.cpp
        src/ui/hostui/menu.cpp
        src/ui/controls_page.cpp
        src/ui/graphics_page.cpp
    )
    # Away from the game's staged runtime, which is only as new as the last game build;
    # run with rexglue-artifacts on PATH.
    set_target_properties(fm4_unit_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fm4-tests")
    target_include_directories(fm4_unit_tests PRIVATE src)
    target_compile_definitions(fm4_unit_tests PRIVATE
        FM4_TEST_GAME_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../extracted/fm4"
        FM4_TEST_STRINGS="${CMAKE_CURRENT_SOURCE_DIR}/../frontend-settings/data/raw/strings/EN.tsv")
    if(WIN32)
        target_link_libraries(fm4_unit_tests PRIVATE Catch2::Catch2WithMain rexruntime bcrypt)
    else()
        target_link_libraries(fm4_unit_tests PRIVATE Catch2::Catch2WithMain rexruntime)
    endif()
endif()

if(NOT FM4_TESTS_ONLY)
fm4_static_recomp_add_generated_module(
    fm4_static_recomp_SpeechFacade_default speech
    "${FM4_STATIC_RECOMP_SPEECH_GENERATED_SOURCES}")
fm4_static_recomp_add_generated_module(
    fm4_static_recomp_XMediaFacade_default xmedia
    "${FM4_STATIC_RECOMP_XMEDIA_GENERATED_SOURCES}")

endif()

# Explicit GPU check; no game data is read and normal release builds do not run it.
option(FM4_BUILD_UPSCALER_SMOKE "Build the macOS Vulkan temporal upscaler GPU smoke test" OFF)
if(APPLE AND FM4_BUILD_UPSCALER_SMOKE)
    add_executable(fm4_upscaler_smoke EXCLUDE_FROM_ALL tests/macos_upscaler_smoke.cpp)
    target_link_libraries(fm4_upscaler_smoke PRIVATE rex::runtime)
endif()

# Uses real host page faults; no game files or GPU are required.
option(FM4_BUILD_MACOS_MEMORY_TESTS "Build macOS guest/GPU page-coherency regression" OFF)
if(APPLE AND FM4_BUILD_MACOS_MEMORY_TESTS)
    add_executable(fm4_macos_page_watch_test EXCLUDE_FROM_ALL
        tests/macos_page_watch_test.cpp
        "${REXSDK_DIR}/src/graphics/shared_memory.cpp")
    target_link_libraries(fm4_macos_page_watch_test PRIVATE rex::runtime)
endif()
