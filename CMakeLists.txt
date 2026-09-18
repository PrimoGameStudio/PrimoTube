cmake_minimum_required(VERSION 3.21)
project(PrimoTube VERSION 0.1.0
    DESCRIPTION "Modern YouTube client built with Qt and QML"
    HOMEPAGE_URL "https://github.com/PrimoGameStudio/PrimoTube"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia Network OpenGL)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
endif()

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appPrimoTube
    src/main.cpp
    src/videomodel.h
    src/videomodel.cpp
    src/searchbackend.h
    src/searchbackend.cpp
    src/playbackresolver.h
    src/playbackresolver.cpp
    src/downloadbackend.h
    src/downloadbackend.cpp
    src/clipboardhelper.h
    src/clipboardhelper.cpp
    src/dependencychecker.h
    src/dependencychecker.cpp
    src/settingsmanager.h
    src/settingsmanager.cpp
    src/historymanager.h
    src/historymanager.cpp
    src/channelbackend.h
    src/channelbackend.cpp
    src/homefeedbackend.h
    src/homefeedbackend.cpp
    src/channelutils.h
    src/channelutils.cpp
    src/subscriptionmanager.h
    src/subscriptionmanager.cpp
    src/videodetails.h
    src/videodetails.cpp
    src/ytdlpupdater.h
    src/ytdlpupdater.cpp
    src/mpvcontroller.h
    src/mpvcontroller.cpp
    src/mpvitem.h
    src/mpvitem.cpp
    src/queuemanager.h
    src/queuemanager.cpp
    src/mprisadapter.h
    src/mprisadapter.cpp
)

qt_add_qml_module(appPrimoTube
    URI PrimoTube
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/SearchPage.qml
        qml/PlayerPage.qml
        qml/PlayerWindow.qml
        qml/VideoDelegate.qml
        qml/ChannelPage.qml
        qml/SettingsPage.qml
)

target_link_libraries(appPrimoTube PRIVATE
    Qt6::Quick
    Qt6::Multimedia
    Qt6::Network
    Qt6::OpenGL
)

if(MPV_FOUND)
    target_compile_definitions(appPrimoTube PRIVATE PRIMOTUBE_HAS_MPV=1)
    target_link_libraries(appPrimoTube PRIVATE PkgConfig::MPV)
    message(STATUS "PrimoTube: libmpv found, mpv playback enabled")
else()
    message(WARNING "PrimoTube: libmpv not found, falling back to QtMultimedia only")
endif()

if(TARGET Qt6::DBus)
    target_compile_definitions(appPrimoTube PRIVATE PRIMOTUBE_HAS_MPRIS=1)
    target_link_libraries(appPrimoTube PRIVATE Qt6::DBus)
    message(STATUS "PrimoTube: Qt DBus found, MPRIS support enabled")
else()
    message(STATUS "PrimoTube: Qt DBus not found, MPRIS support disabled (stub)")
endif()

enable_testing()
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

# Installed binary is `primotube`; the CMake target keeps its historic name.
set_target_properties(appPrimoTube PROPERTIES OUTPUT_NAME primotube)

install(TARGETS appPrimoTube
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES primotube.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES primotube.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

install(FILES org.primotube.primotube.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
