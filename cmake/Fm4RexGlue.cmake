# ShiftGlue integration for the local/generated split, adapted from Pinyon
# Shift's cmake/PinyonShiftRexGlue.cmake.

set(REXSDK_DIR "" CACHE PATH "Path to the ShiftGlue SDK source tree")
set(REXSDK_PREFIX "" CACHE PATH "Path to a prebuilt ShiftGlue SDK (tools/package-sdk.ps1)")
if(NOT REXSDK_DIR AND NOT REXSDK_PREFIX)
    message(FATAL_ERROR "Set REXSDK_DIR to the ShiftGlue source tree or REXSDK_PREFIX to a prebuilt "
        "SDK; use the checked-in presets")
endif()

set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "Disable Tracy networking" FORCE)
option(FM4_STATIC_RECOMP_TRACE_IMPORTS
    "Record first-use guest import reachability diagnostics" ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(NOT CMAKE_C_FLAGS MATCHES "(^| )-msse4\\.1($| )" OR
       NOT CMAKE_CXX_FLAGS MATCHES "(^| )-msse4\\.1($| )")
        message(FATAL_ERROR
            "The AMD64 build must compile C and C++ with -msse4.1; use the checked-in presets")
    endif()
endif()

if(REXSDK_PREFIX)
    find_package(rexglue CONFIG REQUIRED PATHS "${REXSDK_PREFIX}" NO_DEFAULT_PATH)
    # The SDK's own upscaler rules, run here as an in-tree SDK runs them at configure: they find
    # the files tools/build.ps1 fetched and name the runtimes rexglue_configure_target stages.
    include("${REXSDK_PREFIX}/cmake/rexglue_dlss.cmake")
    include("${REXSDK_PREFIX}/cmake/rexglue_ffx_api_headers.cmake")
    add_library(renderdoc ALIAS rex::renderdoc)
    set(FM4_STATIC_RECOMP_REXGLUE_CODEGEN
        "${REXSDK_PREFIX}/bin/rexglue${CMAKE_HOST_EXECUTABLE_SUFFIX}"
        CACHE FILEPATH "The standalone ShiftGlue generator")
else()
    # Keep the consumer build's runtime, GPU plugin and tools inside its own binary
    # tree so a host relink cannot overwrite the SDK's standalone out/ artifacts.
    set(REXGLUE_OUTPUT_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/rexglue-artifacts"
        CACHE PATH "ShiftGlue artifacts for the FM4 host build" FORCE)
    add_subdirectory("${REXSDK_DIR}" rexglue-sdk EXCLUDE_FROM_ALL)
    set(FM4_STATIC_RECOMP_REXGLUE_CODEGEN
        "${REXSDK_DIR}/out/win-amd64/Release/rexglue${CMAKE_HOST_EXECUTABLE_SUFFIX}"
        CACHE FILEPATH "The standalone ShiftGlue generator")
endif()
if(NOT EXISTS "${FM4_STATIC_RECOMP_REXGLUE_CODEGEN}")
    message(FATAL_ERROR
        "The standalone generator is missing. Run tools/codegen.ps1 before configuring.")
endif()
if(TARGET rexruntime AND FM4_STATIC_RECOMP_TRACE_IMPORTS)
    target_compile_definitions(rexruntime PRIVATE REXGLUE_TRACE_IMPORTS=1)
endif()

set(FM4_STATIC_RECOMP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/config/rexglue/fm4_manifest.toml")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/rexglue/fm4_manifest.local.toml")
    set(FM4_STATIC_RECOMP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/config/rexglue/fm4_manifest.local.toml")
endif()
set(FM4_STATIC_RECOMP_GENERATED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.local/generated")
set(FM4_STATIC_RECOMP_GENERATED_DIR "${FM4_STATIC_RECOMP_GENERATED_ROOT}/default")
set(FM4_STATIC_RECOMP_CODEGEN_LOG "${CMAKE_CURRENT_SOURCE_DIR}/.local/logs/codegen.log")
set(REXGLUE_HOST_TARGET fm4_static_recomp)

if(NOT EXISTS "${FM4_STATIC_RECOMP_GENERATED_DIR}/sources.cmake")
    message(FATAL_ERROR
        "Local generated source is missing. Run tools/codegen.ps1 before configuring.")
endif()
include("${FM4_STATIC_RECOMP_GENERATED_DIR}/sources.cmake")
set(FM4_STATIC_RECOMP_GENERATED_SOURCES ${GENERATED_SOURCES})
# Runtime::Setup registers the entrypoint image through PPCFuncMappings.
# Only the facade DLLs need the separate generated registration entry point.
list(FILTER FM4_STATIC_RECOMP_GENERATED_SOURCES EXCLUDE REGEX "/fm4_static_recomp_register\\.cpp$")

foreach(_module IN ITEMS speech xmedia)
    set(_module_dir "${FM4_STATIC_RECOMP_GENERATED_ROOT}/${_module}")
    if(NOT EXISTS "${_module_dir}/sources.cmake")
        message(FATAL_ERROR "Local generated ${_module} source is missing.")
    endif()
    include("${_module_dir}/sources.cmake")
    string(TOUPPER "${_module}" _module_upper)
    set(FM4_STATIC_RECOMP_${_module_upper}_GENERATED_SOURCES ${GENERATED_SOURCES})
endforeach()
unset(GENERATED_SOURCES)

# Generated guest code uses Windows SEH scopes and therefore must compile with
# asynchronous exceptions; the option must also be present while each target's
# PCH is built.
set(REXGLUE_RECOMP_OPTIONS "")
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND REXGLUE_RECOMP_OPTIONS -fasync-exceptions)
endif()
list(APPEND REXGLUE_RECOMP_OPTIONS
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)

set(_all_generated_sources
    ${FM4_STATIC_RECOMP_GENERATED_SOURCES}
    ${FM4_STATIC_RECOMP_SPEECH_GENERATED_SOURCES}
    ${FM4_STATIC_RECOMP_XMEDIA_GENERATED_SOURCES})
set_source_files_properties(${_all_generated_sources}
    PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")

function(fm4_static_recomp_apply_recomp_settings target_name generated_directory)
    target_precompile_headers(${target_name} PRIVATE
        "${generated_directory}/fm4_static_recomp_pch.h")
    target_compile_options(${target_name} PRIVATE ${REXGLUE_RECOMP_OPTIONS})
endfunction()

# The entrypoint stamp's depfile records the manifest, included analysis TOMLs,
# all three game binaries and the SDK version.
add_custom_command(
    OUTPUT "${FM4_STATIC_RECOMP_GENERATED_DIR}/codegen.build.stamp"
    BYPRODUCTS
        "${FM4_STATIC_RECOMP_GENERATED_DIR}/codegen.d"
        ${_all_generated_sources}
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/.local/logs"
    COMMAND "${FM4_STATIC_RECOMP_REXGLUE_CODEGEN}"
        --log-level info
        --log-file "${FM4_STATIC_RECOMP_CODEGEN_LOG}"
        codegen "${FM4_STATIC_RECOMP_MANIFEST}" --ignore-stamp
    DEPENDS "${FM4_STATIC_RECOMP_REXGLUE_CODEGEN}" "${FM4_STATIC_RECOMP_MANIFEST}"
    DEPFILE "${FM4_STATIC_RECOMP_GENERATED_DIR}/codegen.d"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating dependency-tracked recompiled code for FM4"
    VERBATIM)
add_custom_target(fm4_static_recomp_codegen
    DEPENDS "${FM4_STATIC_RECOMP_GENERATED_DIR}/codegen.build.stamp")

set(FM4_STATIC_RECOMP_STAGE_FILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/StageFile.cmake")

function(fm4_static_recomp_attach_rexglue target_name)
    add_library(${target_name}_recomp OBJECT ${FM4_STATIC_RECOMP_GENERATED_SOURCES})
    target_include_directories(${target_name}_recomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${FM4_STATIC_RECOMP_GENERATED_DIR}")
    target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
    rexglue_apply_target_settings(${target_name}_recomp)
    fm4_static_recomp_apply_recomp_settings(${target_name}_recomp "${FM4_STATIC_RECOMP_GENERATED_DIR}")
    add_dependencies(${target_name}_recomp fm4_static_recomp_codegen)
    target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)

    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${FM4_STATIC_RECOMP_GENERATED_DIR}")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    add_dependencies(${target_name} fm4_static_recomp_codegen)
    rexglue_configure_target(${target_name} GPU_PLUGINS fh1)
    # A prebuilt SDK installs imgui's headers and rex/version.h beside its own.
    if(NOT REXSDK_PREFIX)
        # The source-tree helper injects rex_app.cpp into the consumer, while
        # imgui remains a private SDK dependency.
        target_include_directories(${target_name} PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
        # rex/version.h is configured into the SDK sub-build and is needed only by
        # the injected rex_app.cpp consumer source.
        set_property(SOURCE "${REXGLUE_SHARE_DIR}/rex_app.cpp" APPEND PROPERTY
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk/include")
    endif()

    # The SDK helper copies runtime DLLs only after the host links; this target
    # keeps them exact after an SDK-only relink.
    add_custom_target(${target_name}_stage_rexruntime ALL
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE=$<TARGET_FILE:rex::runtime>
            -DDESTINATION=$<TARGET_FILE_DIR:${target_name}>/$<TARGET_FILE_NAME:rex::runtime>
            -P ${FM4_STATIC_RECOMP_STAGE_FILE_SCRIPT}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE=$<TARGET_FILE:rex::gpu-fh1>
            -DDESTINATION=$<TARGET_FILE_DIR:${target_name}>/$<TARGET_FILE_NAME:rex::gpu-fh1>
            -P ${FM4_STATIC_RECOMP_STAGE_FILE_SCRIPT}
        DEPENDS ${target_name}
        COMMENT "Staging the current ShiftGlue runtime and graphics backend beside ${target_name}"
        VERBATIM)
endfunction()

function(fm4_static_recomp_add_generated_module target_name generated_directory generated_sources)
    set(_generated_dir "${FM4_STATIC_RECOMP_GENERATED_ROOT}/${generated_directory}")
    add_library(${target_name} SHARED ${generated_sources})
    target_include_directories(${target_name} PRIVATE "${_generated_dir}")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    fm4_static_recomp_apply_recomp_settings(${target_name} "${_generated_dir}")
    add_dependencies(${target_name} fm4_static_recomp_codegen)
    set_target_properties(${target_name} PROPERTIES CXX_VISIBILITY_PRESET hidden)
    rexglue_configure_module_target(${target_name} HOST ${REXGLUE_HOST_TARGET})
endfunction()
