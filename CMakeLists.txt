cmake_minimum_required(VERSION 3.21)

project(LilyGoUI VERSION 1.2.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

include("${CMAKE_CURRENT_SOURCE_DIR}/CM0AppKit.cmake")
_cm0_ensure_app_runtime()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(cm0-appkit-system-status-tests
        tests/test_system_status.cpp)
    target_compile_options(cm0-appkit-system-status-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(cm0-appkit-system-status-tests PRIVATE
        LilyGoUI::SystemStatus)
    add_test(NAME cm0-appkit-system-status
        COMMAND cm0-appkit-system-status-tests)
endif()

set(_appkit_cmake_dir
    "${CMAKE_INSTALL_LIBDIR}/cmake/LilyGoUI")
set(_legacy_appkit_cmake_dir
    "${CMAKE_INSTALL_LIBDIR}/cmake/LilyGoCM0AppKit")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LilyGoUIConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
        CM0AppKit.cmake
        LilyGoUIConfig.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/LilyGoUIConfigVersion.cmake"
        DESTINATION "${_appkit_cmake_dir}"
        COMPONENT appkit_dev)
install(DIRECTORY assets config include patches src third_party
        DESTINATION "${_appkit_cmake_dir}"
        COMPONENT appkit_dev)
install(FILES
        LilyGoCM0AppKitConfig.cmake
        DESTINATION "${_legacy_appkit_cmake_dir}"
        COMPONENT appkit_dev)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/LilyGoUIConfigVersion.cmake"
        DESTINATION "${_legacy_appkit_cmake_dir}"
        RENAME LilyGoCM0AppKitConfigVersion.cmake
        COMPONENT appkit_dev)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "lilygo-ui-appkit-dev")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "LilyGo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "LILYGO UI application SDK")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL appkit_dev)
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_NAME "lilygo-ui-appkit-dev")
set(CPACK_DEBIAN_APPKIT_DEV_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_ARCHITECTURE all)
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_DEPENDS
    "cmake, pkg-config, libdrm-dev")
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_PROVIDES
    "lilygo-cm0-appkit-dev (= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_CONFLICTS "lilygo-cm0-appkit-dev")
set(CPACK_DEBIAN_APPKIT_DEV_PACKAGE_REPLACES "lilygo-cm0-appkit-dev")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
include(CPack)
