# Included right after the SDK's project() by tools/package-sdk.ps1 (CMAKE_PROJECT_ReXGlue_INCLUDE):
# what Fm4RexGlue.cmake sets on the SDK when it builds the SDK in-tree, so the prebuilt SDK is the
# same build. rexruntime exists only once the SDK's CMakeLists has run, hence the deferred call.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL target_compile_definitions rexruntime PRIVATE REXGLUE_TRACE_IMPORTS=1)
