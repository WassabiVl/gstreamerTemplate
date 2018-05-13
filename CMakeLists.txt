cmake_minimum_required(VERSION 3.10)
project(gstreamerTemplate C)

set(CMAKE_C_STANDARD 11)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0
        gstreamer-sdp-1.0
        gstreamer-video-1.0
        gstreamer-app-1.0)
add_executable(gstreamerTemplate
        3mixertemplate.c
        introduction.c
        "template to g-streamer.c"
        TutorialOfPlayBin.c)
target_include_directories(gstreamerTemplate PRIVATE ${GST_INCLUDE_DIRS})
target_link_libraries(gstreamerTemplate ${GST_LIBRARIES})
