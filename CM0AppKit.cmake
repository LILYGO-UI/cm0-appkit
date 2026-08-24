include_guard(GLOBAL)

set(CM0_VENDOR_SLUG "lilygo")
set(CM0_APP_ID_PREFIX "cc.lilygo.cm0")
set(LILYGO_UI_VENDOR_SLUG "lilygo")
set(LILYGO_UI_APP_ID_PREFIX "cc.lilygo.ui")

if(CMAKE_CROSSCOMPILING)
    set(_cm0_simulator_default OFF)
else()
    set(_cm0_simulator_default ON)
endif()
option(CM0_SIMULATOR "Build with the SDL host display" ${_cm0_simulator_default})
option(CM0_FETCH_LVGL
    "Download pinned LVGL only when neither an override nor the bundled source is available"
    OFF)
set(CM0_LVGL_SOURCE_DIR "" CACHE PATH
    "Optional LVGL 9.5.0 source override for AppKit development")

if(DEFINED LVGL_SOURCE_DIR AND NOT CM0_LVGL_SOURCE_DIR)
    set(CM0_LVGL_SOURCE_DIR "${LVGL_SOURCE_DIR}" CACHE PATH
        "Optional LVGL 9.5.0 source override for AppKit development" FORCE)
endif()
set(_cm0_appkit_dir "${CMAKE_CURRENT_LIST_DIR}")
set(_cm0_lvgl_version "9.5.0")
set(_cm0_bundled_lvgl_archive
    "${_cm0_appkit_dir}/third_party/lvgl-${_cm0_lvgl_version}.tar.gz")
set(LV_BUILD_CONF_PATH "${_cm0_appkit_dir}/config/lv_conf.h" CACHE FILEPATH "CM0 LVGL configuration" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Build LVGL ThorVG" FORCE)
set(CONFIG_LV_USE_PRIVATE_API OFF CACHE BOOL "Do not expose LVGL private APIs" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Statically link LVGL into each application" FORCE)

if(NOT TARGET lvgl)
    find_package(PkgConfig REQUIRED)
    find_package(Freetype REQUIRED)
    set(_cm0_freetype_link_libraries Freetype::Freetype)
    set(_cm0_freetype_library "${FREETYPE_LIBRARY_RELEASE}")
    if(NOT _cm0_freetype_library)
        set(_cm0_freetype_library "${FREETYPE_LIBRARIES}")
    endif()
    if(_cm0_freetype_library MATCHES "\\.a$")
        # FindFreetype does not expose the private dependencies needed by a
        # static libfreetype. Some BSPs contain only that archive (or a broken
        # shared-library symlink), so resolve the dependencies explicitly.
        find_library(_cm0_freetype_zlib NAMES z libz.so.1 REQUIRED)
        find_library(_cm0_freetype_bzip2 NAMES bz2 libbz2.so.1.0 REQUIRED)
        find_library(_cm0_freetype_png NAMES png16 png libpng16.so.16 REQUIRED)
        find_library(_cm0_freetype_brotlidec
            NAMES brotlidec libbrotlidec.so.1 REQUIRED)
        find_library(_cm0_freetype_brotlicommon
            NAMES brotlicommon libbrotlicommon.so.1 REQUIRED)
        list(APPEND _cm0_freetype_link_libraries
            "${_cm0_freetype_png}"
            "${_cm0_freetype_zlib}"
            "${_cm0_freetype_bzip2}"
            "${_cm0_freetype_brotlidec}"
            "${_cm0_freetype_brotlicommon}")
    endif()
    if(CM0_SIMULATOR)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    else()
        pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
    endif()

    if(CM0_LVGL_SOURCE_DIR)
        add_subdirectory("${CM0_LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
        set(_cm0_lvgl_source_dir "${CM0_LVGL_SOURCE_DIR}")
        set(_cm0_lvgl_provider "source override")
    elseif(EXISTS "${_cm0_bundled_lvgl_archive}")
        include(FetchContent)
        FetchContent_Declare(cm0_lvgl
            URL "${_cm0_bundled_lvgl_archive}"
            URL_HASH SHA256=280253ea1dbb9dab9aa2cdab56fb8d646c98b04c171ad52956784d1ffde8152e
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
        FetchContent_MakeAvailable(cm0_lvgl)
        set(_cm0_lvgl_source_dir "${cm0_lvgl_SOURCE_DIR}")
        set(_cm0_lvgl_provider "AppKit bundled source")
    elseif(CM0_FETCH_LVGL)
        include(FetchContent)
        FetchContent_Declare(cm0_lvgl
            URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz
            URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
        FetchContent_MakeAvailable(cm0_lvgl)
        set(_cm0_lvgl_source_dir "${cm0_lvgl_SOURCE_DIR}")
        set(_cm0_lvgl_provider "network fallback")
    else()
        message(FATAL_ERROR
            "AppKit's bundled LVGL ${_cm0_lvgl_version} source is missing: "
            "${_cm0_bundled_lvgl_archive}. Reinstall LilyGoUI, set "
            "CM0_LVGL_SOURCE_DIR, or explicitly enable CM0_FETCH_LVGL.")
    endif()

    file(READ "${_cm0_lvgl_source_dir}/lv_version.h" _cm0_lvgl_version_header)
    foreach(_cm0_expected_definition IN ITEMS
            "LVGL_VERSION_MAJOR 9"
            "LVGL_VERSION_MINOR 5"
            "LVGL_VERSION_PATCH 0")
        string(FIND "${_cm0_lvgl_version_header}"
            "${_cm0_expected_definition}" _cm0_version_position)
        if(_cm0_version_position EQUAL -1)
            message(FATAL_ERROR
                "${_cm0_lvgl_provider} does not contain LVGL ${_cm0_lvgl_version}: "
                "missing '${_cm0_expected_definition}' in lv_version.h")
        endif()
    endforeach()
    message(STATUS
        "LilyGoUI: using LVGL ${_cm0_lvgl_version} from ${_cm0_lvgl_provider}")

    if(NOT CM0_SIMULATOR)
        find_program(_cm0_patch_executable patch REQUIRED)
        set(_cm0_drm_patches
            "${_cm0_appkit_dir}/patches/lvgl-9.5.0-drm-recovery.patch"
            "${_cm0_appkit_dir}/patches/lvgl-9.5.0-drm-software-rotation.patch")
        foreach(_cm0_drm_patch IN LISTS _cm0_drm_patches)
            execute_process(
                COMMAND "${_cm0_patch_executable}" -p1 --forward --batch --dry-run
                        --silent -i "${_cm0_drm_patch}"
                WORKING_DIRECTORY "${_cm0_lvgl_source_dir}"
                RESULT_VARIABLE _cm0_patch_dry_run_result
                OUTPUT_QUIET
                ERROR_QUIET)
            if(_cm0_patch_dry_run_result EQUAL 0)
                execute_process(
                    COMMAND "${_cm0_patch_executable}" -p1 --forward --batch
                            --silent -i "${_cm0_drm_patch}"
                    WORKING_DIRECTORY "${_cm0_lvgl_source_dir}"
                    RESULT_VARIABLE _cm0_patch_result
                    ERROR_VARIABLE _cm0_patch_error)
                if(NOT _cm0_patch_result EQUAL 0)
                    message(FATAL_ERROR
                        "Cannot apply CM0 LVGL DRM patch: ${_cm0_patch_error}")
                endif()
            endif()
        endforeach()

        set(_cm0_drm_source
            "${_cm0_lvgl_source_dir}/src/drivers/display/drm/lv_linux_drm.c")
        file(READ "${_cm0_drm_source}" _cm0_drm_source_contents)
        foreach(_cm0_required_marker IN ITEMS
                "bool needs_modeset;"
                "ret = drmSetMaster(fd);"
                "drm_dev->needs_modeset = true;"
                "uint8_t * render_buf;"
                "lv_draw_sw_rotate(px_map, drm_dev->act_buf->map")
            string(FIND "${_cm0_drm_source_contents}"
                "${_cm0_required_marker}" _cm0_marker_position)
            if(_cm0_marker_position EQUAL -1)
                message(FATAL_ERROR
                    "CM0 LVGL DRM patches are not applied: missing '${_cm0_required_marker}'")
            endif()
        endforeach()

    endif()

    if(CM0_SIMULATOR)
        target_compile_definitions(lvgl PUBLIC CM0_APP_SIMULATOR=1)
        set(_cm0_lv_sdl_window_source
            "${_cm0_lvgl_source_dir}/src/drivers/sdl/lv_sdl_window.c")
        if(MSVC)
            set_property(SOURCE "${_cm0_lv_sdl_window_source}"
                TARGET_DIRECTORY lvgl APPEND PROPERTY COMPILE_OPTIONS
                "/FI${_cm0_appkit_dir}/config/lv_sdl_high_dpi.h")
        else()
            set_property(SOURCE "${_cm0_lv_sdl_window_source}"
                TARGET_DIRECTORY lvgl APPEND PROPERTY COMPILE_OPTIONS
                "-include${_cm0_appkit_dir}/config/lv_sdl_high_dpi.h")
        endif()
        target_link_libraries(lvgl PUBLIC
            PkgConfig::SDL2 ${_cm0_freetype_link_libraries})
    else()
        target_link_libraries(lvgl PUBLIC
            PkgConfig::DRM ${_cm0_freetype_link_libraries})
    endif()
endif()

if(NOT TARGET LilyGoUI::LVGL)
    add_library(LilyGoUI::LVGL ALIAS lvgl)
endif()
if(NOT TARGET LilyGoCM0AppKit::LVGL)
    add_library(LilyGoCM0AppKit::LVGL ALIAS lvgl)
endif()

enable_language(CXX)

function(_cm0_ensure_ui_fonts)
    if(TARGET cm0_ui_fonts)
        return()
    endif()
    target_sources(lvgl PRIVATE "${_cm0_appkit_dir}/src/typography.cpp")
    target_include_directories(lvgl PRIVATE "${_cm0_appkit_dir}/include")
    target_compile_features(lvgl PRIVATE cxx_std_17)
    target_compile_definitions(lvgl PRIVATE
        LILYGO_UI_FONT_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}/lilygo-ui/fonts"
        LILYGO_UI_FONT_SOURCE_DIR="${_cm0_appkit_dir}/assets/fonts")
    add_library(cm0_ui_fonts INTERFACE)
    target_include_directories(cm0_ui_fonts INTERFACE
        "${_cm0_appkit_dir}/include")
    target_link_libraries(cm0_ui_fonts INTERFACE lvgl)
    add_library(LilyGoCM0AppKit::UIFonts ALIAS cm0_ui_fonts)
    add_library(LilyGoUI::UIFonts ALIAS cm0_ui_fonts)
endfunction()

_cm0_ensure_ui_fonts()

function(_cm0_ensure_system_status)
    if(TARGET cm0_system_status)
        return()
    endif()
    add_library(cm0_system_status STATIC
        "${_cm0_appkit_dir}/src/system_status.cpp")
    target_include_directories(cm0_system_status PUBLIC
        "${_cm0_appkit_dir}/include")
    target_compile_features(cm0_system_status PUBLIC cxx_std_17)
    target_compile_options(cm0_system_status PRIVATE -Wall -Wextra -Wpedantic)
    add_library(LilyGoCM0AppKit::SystemStatus ALIAS cm0_system_status)
    add_library(LilyGoUI::SystemStatus ALIAS cm0_system_status)
endfunction()

_cm0_ensure_system_status()

function(_cm0_ensure_status_bar)
    if(TARGET cm0_status_bar)
        return()
    endif()
    add_library(cm0_status_bar STATIC
        "${_cm0_appkit_dir}/src/status_bar.cpp"
        "${_cm0_appkit_dir}/src/icons/status_icon_ethernet.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_keyboard.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_wifi_high.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_wifi_mid.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_wifi_low.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_wifi_zero.c"
        "${_cm0_appkit_dir}/src/icons/status_icon_wifi_no.c")
    target_include_directories(cm0_status_bar PUBLIC
        "${_cm0_appkit_dir}/include")
    target_compile_features(cm0_status_bar PUBLIC cxx_std_17)
    target_compile_options(cm0_status_bar PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(cm0_status_bar PUBLIC
        cm0_ui_fonts cm0_system_status)
    add_library(LilyGoCM0AppKit::StatusBar ALIAS cm0_status_bar)
    add_library(LilyGoUI::StatusBar ALIAS cm0_status_bar)
endfunction()

_cm0_ensure_status_bar()

function(_cm0_ensure_app_runtime)
    if(TARGET cm0_app_runtime)
        return()
    endif()
    add_library(cm0_app_runtime STATIC "${_cm0_appkit_dir}/src/runtime.cpp")
    target_include_directories(cm0_app_runtime PUBLIC
        "${_cm0_appkit_dir}/include")
    target_compile_features(cm0_app_runtime PUBLIC cxx_std_17)
    target_compile_options(cm0_app_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(cm0_app_runtime PUBLIC cm0_status_bar)
    if(CM0_SIMULATOR)
        target_compile_definitions(cm0_app_runtime PRIVATE CM0_APP_SIMULATOR=1)
    endif()
    add_library(LilyGoCM0AppKit::Runtime ALIAS cm0_app_runtime)
    add_library(LilyGoUI::Runtime ALIAS cm0_app_runtime)
endfunction()

_cm0_ensure_app_runtime()

function(lilygo_ui_configure_app_target target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "lilygo_ui_configure_app_target: unknown target ${target}")
    endif()
    _cm0_ensure_app_runtime()
    set(_app_main "${CMAKE_CURRENT_BINARY_DIR}/${target}_main.c")
    file(GENERATE OUTPUT "${_app_main}" CONTENT
"#include <cm0/app.h>\nextern const cm0_app_descriptor_t *cm0_app_get_descriptor(void);\nint main(int argc, char **argv)\n{\n    return cm0_app_run(argc, argv, cm0_app_get_descriptor());\n}\n")
    target_sources(${target} PRIVATE "${_app_main}")
    target_link_libraries(${target} PRIVATE LilyGoUI::Runtime)
endfunction()

function(cm0_configure_app_target target)
    lilygo_ui_configure_app_target(${target})
endfunction()

function(lilygo_ui_install_launcher_icon app_id icon_file)
    if("${app_id}" STREQUAL "" OR "${icon_file}" STREQUAL "")
        message(FATAL_ERROR
            "lilygo_ui_install_launcher_icon requires an application ID and PNG file")
    endif()
    get_filename_component(_cm0_launcher_icon "${icon_file}"
        ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_cm0_launcher_icon}")
        message(FATAL_ERROR "Launcher icon does not exist: ${_cm0_launcher_icon}")
    endif()
    install(FILES "${_cm0_launcher_icon}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps"
        RENAME "${app_id}.png"
        COMPONENT app)
endfunction()

function(cm0_install_launcher_icon app_id icon_file)
    lilygo_ui_install_launcher_icon("${app_id}" "${icon_file}")
endfunction()

function(lilygo_ui_install_font_licenses destination component)
    if("${destination}" STREQUAL "" OR "${component}" STREQUAL "")
        message(FATAL_ERROR
            "lilygo_ui_install_font_licenses requires a destination and component")
    endif()
    install(FILES
        "${_cm0_appkit_dir}/assets/fonts/licenses/NOTICE.md"
        "${_cm0_appkit_dir}/assets/fonts/licenses/OFL-1.1.txt"
        DESTINATION "${destination}"
        COMPONENT "${component}")
endfunction()

function(cm0_install_font_licenses destination component)
    lilygo_ui_install_font_licenses("${destination}" "${component}")
endfunction()

macro(lilygo_ui_enable_standalone_package app_slug)
    if("${app_slug}" STREQUAL "")
        message(FATAL_ERROR "lilygo_ui_enable_standalone_package requires an app slug")
    endif()
    set(_cm0_package_slug "${app_slug}")
    lilygo_ui_install_font_licenses(
        "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_VENDOR_SLUG}-ui-${_cm0_package_slug}"
        app)
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME "${LILYGO_UI_VENDOR_SLUG}-ui-${_cm0_package_slug}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "LilyGo")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "LILYGO UI ${_cm0_package_slug} application")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL app)
    set(CPACK_DEBIAN_APP_PACKAGE_NAME
        "${LILYGO_UI_VENDOR_SLUG}-ui-${_cm0_package_slug}")
    set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
        "libc6, libstdc++6, libdrm2, libfreetype6, lilygo-ui-appkit-dev")
    set(CPACK_DEBIAN_APP_PACKAGE_PROVIDES
        "${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug} (= ${PROJECT_VERSION})")
    set(CPACK_DEBIAN_APP_PACKAGE_CONFLICTS
        "${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug}")
    set(CPACK_DEBIAN_APP_PACKAGE_REPLACES
        "${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    endif()
    include(CPack)
endmacro()

macro(cm0_enable_standalone_package app_slug)
    if("${app_slug}" STREQUAL "")
        message(FATAL_ERROR "cm0_enable_standalone_package requires an app slug")
    endif()
    set(_cm0_package_slug "${app_slug}")
    cm0_install_font_licenses(
        "${CMAKE_INSTALL_DATAROOTDIR}/doc/${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug}"
        app)
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME "${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "LilyGo")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Independent CM0 ${_cm0_package_slug} application")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL app)
    set(CPACK_DEBIAN_APP_PACKAGE_NAME
        "${CM0_VENDOR_SLUG}-cm0-${_cm0_package_slug}")
    set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
        "libc6, libstdc++6, libdrm2, libfreetype6, lilygo-ui-appkit-dev")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    endif()
    include(CPack)
endmacro()
