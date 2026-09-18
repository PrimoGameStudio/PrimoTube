find_package(Qt6 REQUIRED COMPONENTS Test Network Quick Multimedia OpenGL)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS DBus)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND AND NOT MPV_FOUND)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
endif()

add_executable(tst_primotube
    tst_primotube.cpp
    ../src/settingsmanager.cpp
    ../src/videomodel.cpp
    ../src/dependencychecker.cpp
    ../src/playbackresolver.cpp
    ../src/searchbackend.cpp
    ../src/historymanager.cpp
    ../src/channelbackend.cpp
    ../src/homefeedbackend.cpp
    ../src/channelutils.cpp
    ../src/subscriptionmanager.cpp
    ../src/videodetails.cpp
    ../src/downloadbackend.cpp
    ../src/ytdlpupdater.cpp
    ../src/mpvcontroller.cpp
    ../src/queuemanager.cpp
    ../src/mprisadapter.cpp
)
target_link_libraries(tst_primotube PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Network
    Qt6::Quick
    Qt6::Multimedia
    Qt6::OpenGL
)

if(MPV_FOUND)
    target_compile_definitions(tst_primotube PRIVATE PRIMOTUBE_HAS_MPV=1)
    target_link_libraries(tst_primotube PRIVATE PkgConfig::MPV)
endif()

if(TARGET Qt6::DBus)
    target_compile_definitions(tst_primotube PRIVATE PRIMOTUBE_HAS_MPRIS=1)
    target_link_libraries(tst_primotube PRIVATE Qt6::DBus)
endif()

add_test(NAME tst_primotube COMMAND tst_primotube)
set_tests_properties(tst_primotube PROPERTIES TIMEOUT 120)
