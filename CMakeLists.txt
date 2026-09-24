cmake_minimum_required(VERSION 3.31)
project(RealTimeDataMonitor VERSION 1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "Real-Time Data Monitor requires Windows 10 or later.")
endif()

add_library(monitor_core
    src/core/measurement.cpp
    src/core/monitor.cpp
    src/system/meminfo.cpp
    src/system/processinfo.cpp
    src/system/stat.cpp
    src/system/uptime.cpp
    src/ui/presentation.cpp
    src/app/settings.cpp
)
target_include_directories(monitor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(monitor_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(monitor_core PUBLIC psapi shell32 ole32)

add_executable(untitled7 WIN32
    src/app/main.cpp
    src/ui/gui.cpp
    resources/app.rc
)

target_include_directories(untitled7 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_link_libraries(untitled7 PRIVATE monitor_core user32 gdi32 comctl32 comdlg32 uxtheme)
if(MSVC)
    target_link_options(untitled7 PRIVATE /MANIFEST:NO)
endif()

foreach(target monitor_core untitled7)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()
