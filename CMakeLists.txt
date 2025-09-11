cmake_minimum_required(VERSION 3.20)
project(consoleAppStaticPlayer)

set(CMAKE_CXX_STANDARD 20)

include_directories(
        include/choc/audio
        include
)

add_executable(consoleAppStaticPlayer main.cpp
        AudioEngine.h
        AudioModule.h
        AudioFilePlayerModule.cpp
        AudioFilePlayerModule.h
        UdpSender.h
        App.h)



# Find threading library (needed for both platforms)
find_package(Threads REQUIRED)

if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
    
    target_link_libraries(consoleAppStaticPlayer
        ${FOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        Threads::Threads
    )
elseif(UNIX AND NOT APPLE)
    # Linux - find ALSA
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    
    target_include_directories(consoleAppStaticPlayer PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(consoleAppStaticPlayer
        ${ALSA_LIBRARIES}
        Threads::Threads
    )
    
    # Debug output
    message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
    message(STATUS "ALSA include dirs: ${ALSA_INCLUDE_DIRS}")
endif()
