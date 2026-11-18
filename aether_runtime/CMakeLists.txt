cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AetherEntisGLS LANGUAGES C CXX)
    enable_testing()
    if(NOT AETHER_ENTISGLS_HOST_ROOT)
        get_filename_component(AETHER_ENTISGLS_HOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
endif()

if(NOT EXISTS "${AETHER_ENTISGLS_HOST_ROOT}/abi/include/engine_api.h")
    message(FATAL_ERROR "Set AETHER_ENTISGLS_HOST_ROOT to an Aether checkout containing abi/include")
endif()

set(_entis_default_tests ON)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_entis_default_tests OFF)
endif()
option(AETHERKIRI_BUILD_ENTISGLS_TESTS "Build asset-free EntisGLS regression tests" ${_entis_default_tests})
unset(_entis_default_tests)
if(NOT TARGET aether_game_hooks)
    add_subdirectory("${AETHER_ENTISGLS_HOST_ROOT}/bridge/game_hooks" "${CMAKE_CURRENT_BINARY_DIR}/game_hooks")
endif()
# The SDK is large; retain source-line crash diagnostics without duplicating
# full vendor type information into every Debug object and archive.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<CONFIG:Debug>:-gline-tables-only>)
endif()
set(AETHER_ENTISGLS_EMBEDDED ON)
set(ENTISGLS_LAUNCHER ON)
set(STUDYSTEADY_SDL3 ON)
find_package(Freetype REQUIRED)
if(NOT TARGET freetype)
    add_library(freetype ALIAS Freetype::Freetype)
endif()
get_filename_component(entis_vendor "${CMAKE_CURRENT_SOURCE_DIR}/.." REALPATH)
add_subdirectory("${entis_vendor}" "${CMAKE_CURRENT_BINARY_DIR}/sdk")

find_package(nlohmann_json CONFIG REQUIRED)
add_library(aether_entisgls_runtime SHARED src/provider.cpp src/input.cpp src/font_discovery.cpp src/storage.cpp src/game_hooks.cpp
    "${entis_vendor}/native/launcher/game_config.cpp"
    "${entis_vendor}/native/launcher/molebox_resources.cpp"
    "${entis_vendor}/native/launcher/config_encoding.cpp"
    "${entis_vendor}/native/launcher/archive_launch_fallback.cpp"
    "${entis_vendor}/native/launcher/runtime_session.cpp"
    "${entis_vendor}/native/runtime/witch/script.cpp"
    "${entis_vendor}/native/runtime/witch/player.cpp"
    "${entis_vendor}/native/compatibility/games/compatibility_profiles.cpp"
    "${entis_vendor}/native/platform/sdl/game_font_aliases.cpp"
    "${entis_vendor}/native/platform/sdl/opentype_font.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/sdk/legacy_heap_sdk/glscs_sakura2_obj_heap.cpp")
target_compile_features(aether_entisgls_runtime PRIVATE cxx_std_17)
target_compile_definitions(aether_entisgls_runtime PRIVATE AETHER_ENTISGLS_EXPORTS=1 SDL_MAIN_HANDLED=1)
target_include_directories(aether_entisgls_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${AETHER_ENTISGLS_HOST_ROOT}/abi/include")
target_link_libraries(aether_entisgls_runtime PRIVATE legacy_objects legacy_foundation gls4_sdl
    motion_apk_runtime freetype nlohmann_json::nlohmann_json aether_game_hooks)
if(ANDROID)
    target_link_libraries(aether_entisgls_runtime PRIVATE EGL)
    add_executable(aether_entisgls_android_host_test EXCLUDE_FROM_ALL tests/android_host_test.cpp)
    target_link_libraries(aether_entisgls_android_host_test PRIVATE SDL3::SDL3 GLESv3)
endif()
set_target_properties(aether_entisgls_runtime PROPERTIES CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(APPLE)
    target_link_options(aether_entisgls_runtime PRIVATE "LINKER:-exported_symbol,_AetherEntisGLSProvider")
    set_target_properties(aether_entisgls_runtime PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # Keep SDL3/TJS/SDK globals in their own image, just like desktop and
        # Android. Merging these objects into Godot's archive aliases its SDL
        # and the other engines' TJS symbols. iOS embeds a signed framework.
        set(AETHER_ENTISGLS_IOS_PLATFORM iPhoneOS)
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(AETHER_ENTISGLS_IOS_PLATFORM iPhoneSimulator)
        endif()
        set_target_properties(aether_entisgls_runtime PROPERTIES
            FRAMEWORK TRUE
            OUTPUT_NAME AetherEntisGLS
            MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios_framework.plist.in"
            MACOSX_FRAMEWORK_IDENTIFIER org.aetherkiri.entisgls
            MACOSX_FRAMEWORK_SHORT_VERSION_STRING 1.0
            MACOSX_FRAMEWORK_BUNDLE_VERSION 1
            INSTALL_NAME_DIR "@rpath"
            BUILD_WITH_INSTALL_NAME_DIR TRUE)
    endif()
elseif(UNIX)
    target_link_options(aether_entisgls_runtime PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:-z,defs")
    set_target_properties(aether_entisgls_runtime PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
endif()

option(AETHERKIRI_BUILD_ENTISGLS_IOS_TESTS "Build the UIKit offscreen integration test app" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND AETHERKIRI_BUILD_ENTISGLS_IOS_TESTS)
    add_executable(aether_entisgls_ios_host_test MACOSX_BUNDLE tests/ios_host_test.mm
        "${entis_vendor}/native/platform/ios/ios_gl_context.mm")
    target_include_directories(aether_entisgls_ios_host_test PRIVATE "${entis_vendor}/native")
    target_compile_options(aether_entisgls_ios_host_test PRIVATE -fobjc-arc)
    target_link_libraries(aether_entisgls_ios_host_test PRIVATE SDL3::SDL3
        "-framework UIKit" "-framework OpenGLES")
    set_target_properties(aether_entisgls_ios_host_test PROPERTIES
        OBJCXX_STANDARD 17
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/tests/ios_host_test.plist.in")
endif()

if(AETHERKIRI_BUILD_ENTISGLS_TESTS)
    add_executable(aether_entisgls_witch_test EXCLUDE_FROM_ALL
        "${entis_vendor}/tests/unit/witch/script_test.cpp" "${entis_vendor}/native/runtime/witch/script.cpp")
    target_compile_features(aether_entisgls_witch_test PRIVATE cxx_std_17)
    target_include_directories(aether_entisgls_witch_test PRIVATE "${entis_vendor}/native")
    target_link_libraries(aether_entisgls_witch_test PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME entisgls-witch-script COMMAND aether_entisgls_witch_test)
    add_executable(aether_entisgls_storage_test EXCLUDE_FROM_ALL tests/storage_test.cpp src/storage.cpp)
    target_compile_features(aether_entisgls_storage_test PRIVATE cxx_std_17)
    target_include_directories(aether_entisgls_storage_test PRIVATE src)
    target_link_libraries(aether_entisgls_storage_test PRIVATE gls4_sdl)
    add_test(NAME entisgls-game-local-storage COMMAND aether_entisgls_storage_test "${CMAKE_CURRENT_BINARY_DIR}/storage-fixture")
    add_executable(aether_entisgls_provider_test EXCLUDE_FROM_ALL tests/provider_test.cpp)
    target_compile_features(aether_entisgls_provider_test PRIVATE cxx_std_17)
    target_link_libraries(aether_entisgls_provider_test PRIVATE aether_entisgls_runtime nlohmann_json::nlohmann_json)
    add_test(NAME entisgls-provider COMMAND aether_entisgls_provider_test "${CMAKE_CURRENT_BINARY_DIR}/csx-fixture" "${CMAKE_CURRENT_BINARY_DIR}/provider-fixture")
    add_executable(aether_entisgls_adaptation_test EXCLUDE_FROM_ALL tests/adaptation_test.cpp src/input.cpp src/font_discovery.cpp src/game_hooks.cpp
        "${entis_vendor}/native/runtime/witch/script.cpp"
        "${entis_vendor}/native/launcher/config_encoding.cpp"
        "${entis_vendor}/native/compatibility/games/compatibility_profiles.cpp"
        "${entis_vendor}/native/platform/sdl/game_font_aliases.cpp"
        "${entis_vendor}/native/platform/sdl/opentype_font.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/sdk/legacy_heap_sdk/glscs_sakura2_obj_heap.cpp")
    target_compile_features(aether_entisgls_adaptation_test PRIVATE cxx_std_17)
    target_include_directories(aether_entisgls_adaptation_test PRIVATE src "${AETHER_ENTISGLS_HOST_ROOT}/abi/include")
    target_link_libraries(aether_entisgls_adaptation_test PRIVATE legacy_objects legacy_foundation gls4_sdl motion_apk_runtime freetype aether_game_hooks nlohmann_json::nlohmann_json)
    foreach(mode message-glyphs encoding input button-status edit-options graphics image-cache camera-cache control audio fonts serialization game-hooks witch-sdk legacy-bshf)
        add_test(NAME entisgls-${mode} COMMAND aether_entisgls_adaptation_test ${mode} "${CMAKE_CURRENT_BINARY_DIR}/adaptation-${mode}")
    endforeach()
    add_test(NAME entisgls-configuration COMMAND launcher_config_test "${CMAKE_CURRENT_BINARY_DIR}/config-fixtures")
    add_test(NAME entisgls-packaged-resources COMMAND molebox_resources_test
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/molebox" "${CMAKE_CURRENT_BINARY_DIR}/package-fixture")
    add_test(NAME entisgls-speech-capabilities COMMAND legacy_speech_test)
    add_test(NAME entisgls-font-enumeration COMMAND legacy_setup_fonts_test)
    add_test(NAME entisgls-font-rasterization COMMAND launcher_fonts_test
        "${CMAKE_CURRENT_BINARY_DIR}/font-raster-fixture"
        "${AETHER_ENTISGLS_HOST_ROOT}/apps/godot_app/assets/fonts/aetherkiri-ui.ttf")
    add_executable(aether_entisgls_motion_masks_test EXCLUDE_FROM_ALL tests/motion_masks_test.cpp)
    target_link_libraries(aether_entisgls_motion_masks_test PRIVATE motion_apk_runtime SDL3::SDL3)
    add_test(NAME entisgls-motion-masks COMMAND aether_entisgls_motion_masks_test)
    add_executable(aether_entisgls_motion_timeline_test EXCLUDE_FROM_ALL tests/motion_timeline_test.cpp)
    target_link_libraries(aether_entisgls_motion_timeline_test PRIVATE motion_apk_runtime)
    add_test(NAME entisgls-motion-timelines COMMAND aether_entisgls_motion_timeline_test)
    add_test(NAME entisgls-emote-save-abi COMMAND legacy_emote_api_test)
    add_test(NAME entisgls-atlas-projection COMMAND motion_atlas_projection_test)
    add_test(NAME entisgls-psb-key-resolver COMMAND psb_key_resolver_test)
    add_test(NAME entisgls-psb-key-settings COMMAND psb_key_settings_test "${CMAKE_CURRENT_BINARY_DIR}/key-fixtures")
    add_test(NAME entisgls-save-directory COMMAND game_save_directory_test)
    add_test(NAME entisgls-atomic-save COMMAND legacy_atomic_path_test)
    add_test(NAME entisgls-platform-io COMMAND sdl_system_test "${CMAKE_CURRENT_BINARY_DIR}/io-fixtures")
    add_test(NAME entisgls-orientation COMMAND mobile_orientation_test)
    add_test(NAME entisgls-csx-serialization COMMAND make_csx_fixture "${CMAKE_CURRENT_BINARY_DIR}/csx-fixture")
    set_tests_properties(entisgls-csx-serialization PROPERTIES FIXTURES_SETUP entisgls-csx)
    set_tests_properties(entisgls-provider PROPERTIES FIXTURES_REQUIRED entisgls-csx)
    get_property(entis_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${entis_tests} PROPERTIES TIMEOUT 60
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    set_tests_properties(entisgls-motion-masks PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy" SKIP_RETURN_CODE 77 LABELS "gpu")
    # Integrated CI builds the default target before running the full CTest
    # inventory. Include every registered executable when tests are enabled.
    add_custom_target(aether_entisgls_tests ALL DEPENDS launcher_config_test molebox_resources_test legacy_speech_test aether_entisgls_witch_test
        aether_entisgls_adaptation_test aether_entisgls_provider_test aether_entisgls_storage_test
        legacy_setup_fonts_test launcher_fonts_test aether_entisgls_motion_masks_test aether_entisgls_motion_timeline_test legacy_emote_api_test motion_atlas_projection_test
        psb_key_resolver_test psb_key_settings_test game_save_directory_test legacy_atomic_path_test
        sdl_system_test mobile_orientation_test make_csx_fixture)
endif()
