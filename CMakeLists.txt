cmake_minimum_required(VERSION 3.20)
project(consoleAppStaticPlayer)

set(CMAKE_CXX_STANDARD 20)

include_directories(
        external
        include/
)

add_executable(consoleAppStaticPlayer
        src/main.cpp
        src/AudioFilePlayerModule.cpp
        external/rtaudio/RtAudio.cpp
)

# Find threading library (needed for both platforms)
find_package(Threads REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
    
    # Enable CoreAudio backend for RtAudio
    add_definitions(-D__MACOSX_CORE__)
    
    target_link_libraries(consoleAppStaticPlayer
        ${COREFOUNDATION_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        Threads::Threads
    )
elseif(UNIX AND NOT APPLE)
    # Linux - find ALSA
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    
    # Enable ALSA backend for RtAudio
    add_definitions(-D__LINUX_ALSA__)
    
    target_include_directories(consoleAppStaticPlayer PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(consoleAppStaticPlayer
        ${ALSA_LIBRARIES}
        Threads::Threads
    )
    
    # Debug output
    message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
    message(STATUS "ALSA include dirs: ${ALSA_INCLUDE_DIRS}")
endif()
