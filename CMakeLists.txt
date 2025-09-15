cmake_minimum_required(VERSION 3.20)
project(consoleAppStaticPlayer)

# Apply CHOC JACK disable patch (cross-platform)
execute_process(
    COMMAND sed -i.bak "s/#define __UNIX_JACK__ 1/\\/\\/ #define __UNIX_JACK__ 1/" 
        ${CMAKE_SOURCE_DIR}/external/choc/choc/audio/io/choc_RtAudioPlayer.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE PATCH_RESULT
)
if(PATCH_RESULT EQUAL 0)
    message(STATUS "Applied CHOC JACK disable patch")
else()
    message(WARNING "Failed to apply CHOC JACK patch, may already be applied")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include_directories(
    external
    external/choc
    include/
)

add_executable(consoleAppStaticPlayer
    src/main.cpp
    src/BufferedAudioFilePlayer.cpp
)

if(APPLE)
    target_link_libraries(consoleAppStaticPlayer PRIVATE 
        "-framework CoreAudio" 
        "-framework AudioToolbox" 
        "-framework CoreMIDI" 
        "-framework CoreFoundation"
    )
    add_definitions(-D__MACOSX_CORE__)
    
elseif(UNIX AND NOT APPLE)
    # Linux - find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    find_package(Threads REQUIRED)
    
    
    target_include_directories(consoleAppStaticPlayer PRIVATE 
        ${ALSA_INCLUDE_DIRS}
        ${GTK3_INCLUDE_DIRS}
    )
    
    target_link_libraries(consoleAppStaticPlayer
        ${ALSA_LIBRARIES}
        ${GTK3_LIBRARIES}
        Threads::Threads
    )
    
    # Debug output
    message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
    message(STATUS "ALSA include dirs: ${ALSA_INCLUDE_DIRS}")
    message(STATUS "GTK3 libraries: ${GTK3_LIBRARIES}")
    message(STATUS "GTK3 include dirs: ${GTK3_INCLUDE_DIRS}")
endif()
