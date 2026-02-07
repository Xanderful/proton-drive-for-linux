cmake_minimum_required(VERSION 3.16)
project(proton-drive VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# GTK4 for modern native UI
pkg_check_modules(GTK REQUIRED gtk4)
message(STATUS "Using GTK4 for native UI")
add_definitions(-DUSE_GTK4=1)
add_definitions(-DNATIVE_UI_MODE=1)

pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(SQLITE REQUIRED sqlite3)
pkg_check_modules(OPENSSL REQUIRED openssl)

# Native GTK4 mode uses StatusNotifierItem D-Bus protocol for system tray
message(STATUS "Using StatusNotifierItem D-Bus protocol for system tray")
message(STATUS "  -> Requires a StatusNotifierWatcher (e.g., gnome-shell-extension-appindicator)")

# Source files - core utilities (GTK-independent)
set(CORE_SOURCES
    src/logger.cpp
    src/notifications.cpp
    src/bandwidth_monitor.cpp
    src/settings.cpp
    src/network_monitor.cpp
    src/file_watcher.cpp
    src/device_identity.cpp
    src/sync_job_metadata.cpp
    src/file_index.cpp
    src/crypto.cpp
    src/trash_manager.cpp
)

# Source files - sync logic
set(SYNC_SOURCES
    src/sync_manager.cpp
    src/sync_manager_device.cpp
    src/cloud_browser.cpp
)

# Source files - GTK4 native UI
set(UI_SOURCES
    src/main_native.cpp
    src/app_window.cpp
    src/app_window_helpers.cpp
    src/app_window_cloud.cpp
    src/app_window_menus.cpp
    src/app_window_sync.cpp
    src/app_window_actions.cpp
    src/app_window_polling.cpp
    src/app_window_ui.cpp
    src/app_window_monitor.cpp
    src/tray_gtk4.cpp
)

# All sources
set(SOURCES
    ${UI_SOURCES}
    ${CORE_SOURCES}
    ${SYNC_SOURCES}
)

# Create executable
add_executable(proton-drive ${SOURCES})

# Include directories
target_include_directories(proton-drive PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GTK_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${SQLITE_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
)

# Compiler flags
target_compile_options(proton-drive PRIVATE
    ${GTK_CFLAGS_OTHER}
    -Wall -Wextra
)

# Link libraries
target_link_libraries(proton-drive PRIVATE
    ${GTK_LIBRARIES}
    ${CURL_LIBRARIES}
    ${SQLITE_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# Install target
install(TARGETS proton-drive DESTINATION bin)
install(FILES resources/icons/proton-drive.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES resources/icons/proton-drive-tray.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/proton-drive.desktop DESTINATION share/applications)

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "proton-drive")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Proton Drive desktop client for Linux")
set(CPACK_PACKAGE_CONTACT "Xanderful")
set(CPACK_PACKAGE_VENDOR "Xanderful")

# DEB specific
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-4-1, libcurl4, libsqlite3-0, libssl3 | libssl1.1, rclone")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

# RPM specific  
set(CPACK_RPM_PACKAGE_REQUIRES "gtk4, libcurl, sqlite, rclone")
set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0")

include(CPack)
