cmake_minimum_required(VERSION 3.20)
project(consoleAppStaticPlayer)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include_directories(
        external
        external/choc
        include/
)

add_executable(consoleAppStaticPlayer
        src/main.cpp
)


if(APPLE)
    target_link_libraries(consoleAppStaticPlayer PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreMIDI" "-framework CoreFoundation")

    # Enable CoreAudio backend for RtAudio
    add_definitions(-D__MACOSX_CORE__)
elseif(UNIX AND NOT APPLE)
    # Linux - find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    
    # Find Threads package
    find_package(Threads REQUIRED)
    
    # Enable ALSA backend for RtAudio
    add_definitions(-D__LINUX_ALSA__)
    # Disable JACK audio support
    target_compile_definitions(consoleAppStaticPlayer PRIVATE __UNIX_JACK__=0)
    
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
