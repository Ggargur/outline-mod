cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# ItemOutline - SKSE plugin (Skyrim AE 1.6.x)
# Edge-glow highlight on collectable items under the crosshair.
# Community Shaders / ENB compatible (uses the game's native EffectShader path).
# ---------------------------------------------------------------------------

project(
    ItemOutline
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (vendored as a git submodule under extern/CommonLibSSE-NG)
#   git submodule add https://github.com/CharmedBaryon/CommonLibSSE-NG extern/CommonLibSSE-NG
#
# Build for Anniversary Edition (1.6.x) only, as agreed. CommonLibSSE-NG still
# produces a runtime-flexible DLL, but we only target/test AE addresses.
# ---------------------------------------------------------------------------
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE-NG)

# ---------------------------------------------------------------------------
# SimpleIni (header-only) via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    SimpleIni
    GIT_REPOSITORY https://github.com/brofield/simpleini.git
    GIT_TAG        v4.22
)
FetchContent_MakeAvailable(SimpleIni)

# ---------------------------------------------------------------------------
# The plugin target
#
# add_commonlibsse_plugin CREATES the SHARED library target, generates the SKSE
# plugin metadata (name/version/author) that SKSE reads from the DLL, and links
# CommonLibSSE-NG. USE_ADDRESS_LIBRARY makes the DLL resolve game addresses via
# the Address Library (required dependency at runtime).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES              ${SOURCES}
    NAME                 "ItemOutline"
    AUTHOR               "outline-mod"
    VERSION              ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${simpleini_SOURCE_DIR}"
)

# ---------------------------------------------------------------------------
# Optional: auto-deploy to a mod staging folder for MO2/Vortex development.
# Set -DITEMOUTLINE_DEPLOY_DIR="C:/path/to/mods/ItemOutline/SKSE/Plugins" to enable.
# ---------------------------------------------------------------------------
if(DEFINED ITEMOUTLINE_DEPLOY_DIR AND NOT ITEMOUTLINE_DEPLOY_DIR STREQUAL "")
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ITEMOUTLINE_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${PROJECT_NAME}>" "${ITEMOUTLINE_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${ITEMOUTLINE_DEPLOY_DIR}"
        COMMENT "Deploying ItemOutline.dll to ${ITEMOUTLINE_DEPLOY_DIR}"
    )
endif()
