#include <iostream>
#include <thread>
#include <chrono>

#include "AudioGenerator.h"
#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_MessageLoop.h"

int main()
{
    std::cout << "CHOC Audio Beep Player Example" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "This example will play a 440Hz beep for 2 seconds using RtAudioMIDIPlayer." << std::endl;
    std::cout << std::endl;

    choc::audio::io::AudioDeviceOptions options;
    options.sampleRate = 96000;
    options.blockSize = 64;
    options.outputChannelCount = 2;
    options.inputChannelCount = 0;

    auto logMessage = [] (const std::string& message)
    {
        std::cout << "[Audio] " << message << std::endl;
    };

    choc::audio::io::RtAudioMIDIPlayer player (options, logMessage);

    auto error = player.getLastError();
    if (! error.empty())
    {
        std::cerr << "Error creating audio player: " << error << std::endl;
        return 1;
    }

    std::cout << "Audio setup complete:" << std::endl;
    std::cout << "  Sample rate: " << player.options.sampleRate << " Hz" << std::endl;
    std::cout << "  Block size: " << player.options.blockSize << " samples" << std::endl;
    std::cout << "  Output channels: " << player.options.outputChannelCount << std::endl;
    std::cout << std::endl;

    auto audioGenerator = std::make_unique<AudioGenerator>();
    player.addCallback (*audioGenerator);

    std::cout << "Playing beep (440Hz square wave for 2 seconds)..." << std::endl;

    choc::messageloop::initialise();

    auto startTime = std::chrono::steady_clock::now();

    while (audioGenerator->isStillPlaying())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (50));

        auto elapsed = std::chrono::steady_clock::now() - startTime;

        if (elapsed > std::chrono::seconds (3))
        {
            std::cout << "Timeout reached, stopping..." << std::endl;
            break;
        }
    }

    std::cout << "Beep finished!" << std::endl;

    player.removeCallback (*audioGenerator);
    audioGenerator.reset();

    return 0;
}