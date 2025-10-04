cmake_minimum_required(VERSION 3.20)
project(consoleAudioPlayer)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include_directories(
    external
    external/choc
    include/
)

add_executable(consoleAudioPlayer
    src/main.cpp
    src/BufferedAudioFilePlayer.cpp
    src/JackClient.cpp
)

if(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK REQUIRED jack)

    target_include_directories(consoleAudioPlayer PRIVATE
        ${JACK_INCLUDE_DIRS}
    )

    target_link_directories(consoleAudioPlayer PRIVATE
        ${JACK_LIBRARY_DIRS}
    )

    target_link_libraries(consoleAudioPlayer PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreMIDI"
        "-framework CoreFoundation"
        ${JACK_LIBRARIES}
    )
    add_definitions(-D__MACOSX_CORE__)

    # Debug output
    message(STATUS "JACK include dirs: ${JACK_INCLUDE_DIRS}")
    message(STATUS "JACK library dirs: ${JACK_LIBRARY_DIRS}")
    message(STATUS "JACK libraries: ${JACK_LIBRARIES}")

elseif(UNIX AND NOT APPLE)
    # Linux - find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(JACK REQUIRED jack)
    find_package(Threads REQUIRED)

    target_include_directories(consoleAudioPlayer PRIVATE
        ${ALSA_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
    )

    target_link_libraries(consoleAudioPlayer
        ${ALSA_LIBRARIES}
        ${JACK_LIBRARIES}
        Threads::Threads
    )

    # Debug output
    message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
    message(STATUS "ALSA include dirs: ${ALSA_INCLUDE_DIRS}")
    message(STATUS "JACK libraries: ${JACK_LIBRARIES}")
    message(STATUS "JACK include dirs: ${JACK_INCLUDE_DIRS}")
endif()
