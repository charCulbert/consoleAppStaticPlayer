cmake_minimum_required(VERSION 3.20)
project(consoleAudioPlayer)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Common packages for both platforms
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED jack)

target_include_directories(consoleAudioPlayer PRIVATE
    ${JACK_INCLUDE_DIRS}
)

# Platform-specific configuration
if(APPLE)
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

    message(STATUS "JACK library dirs: ${JACK_LIBRARY_DIRS}")

elseif(UNIX AND NOT APPLE)
    pkg_check_modules(ALSA REQUIRED alsa)
    find_package(Threads REQUIRED)

    target_include_directories(consoleAudioPlayer PRIVATE
        ${ALSA_INCLUDE_DIRS}
    )

    target_link_libraries(consoleAudioPlayer PRIVATE
        ${ALSA_LIBRARIES}
        ${JACK_LIBRARIES}
        Threads::Threads
    )

    message(STATUS "ALSA libraries: ${ALSA_LIBRARIES}")
    message(STATUS "ALSA include dirs: ${ALSA_INCLUDE_DIRS}")
endif()

# Common debug output
message(STATUS "JACK include dirs: ${JACK_INCLUDE_DIRS}")
message(STATUS "JACK libraries: ${JACK_LIBRARIES}")
