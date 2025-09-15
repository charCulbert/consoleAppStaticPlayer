#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

#include "AudioGenerator.h"
#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/text/choc_JSON.h"
#include "choc/text/choc_Files.h"

struct Settings {
    int sampleRate = 96000;
    int blockSize = 64;
    int outputChannels = 2;
    int inputChannels = 0;
};

Settings loadSettings() {
    Settings settings;
    const std::string settingsFile = "audio_settings.json";

    try {
        if (std::filesystem::exists(settingsFile)) {
            auto content = choc::file::loadFileAsString(settingsFile);
            auto json = choc::json::parse(content);

            if (json.hasObjectMember("sampleRate"))
                settings.sampleRate = json["sampleRate"].getWithDefault<int>(96000);
            if (json.hasObjectMember("blockSize"))
                settings.blockSize = json["blockSize"].getWithDefault<int>(64);
            if (json.hasObjectMember("outputChannels"))
                settings.outputChannels = json["outputChannels"].getWithDefault<int>(2);
            if (json.hasObjectMember("inputChannels"))
                settings.inputChannels = json["inputChannels"].getWithDefault<int>(0);
        }
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not load settings, using defaults: " << e.what() << std::endl;
    }

    return settings;
}

void saveSettings(const Settings& settings) {
    try {
        auto json = choc::json::create(
            "sampleRate", settings.sampleRate,
            "blockSize", settings.blockSize,
            "outputChannels", settings.outputChannels,
            "inputChannels", settings.inputChannels
        );

        auto jsonString = choc::json::toString(json, true);
        choc::file::replaceFileWithContent("audio_settings.json", jsonString);
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not save settings: " << e.what() << std::endl;
    }
}

std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> createAudioPlayer(const Settings& settings, auto logMessage) {
    choc::audio::io::AudioDeviceOptions options;
    options.sampleRate = settings.sampleRate;
    options.blockSize = settings.blockSize;
    options.outputChannelCount = settings.outputChannels;
    options.inputChannelCount = settings.inputChannels;

    auto player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(options, logMessage);

    if (!player->getLastError().empty()) {
        return nullptr;
    }

    return player;
}

int main()
{
    std::cout << "CHOC Audio Beep Player Example" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "This example will play a 440Hz beep for 2 seconds using RtAudioMIDIPlayer." << std::endl;
    std::cout << std::endl;

    auto settings = loadSettings();

    std::cout << "Loaded settings:" << std::endl;
    std::cout << "  Sample rate: " << settings.sampleRate << " Hz" << std::endl;
    std::cout << "  Block size: " << settings.blockSize << " samples" << std::endl;
    std::cout << "  Output channels: " << settings.outputChannels << std::endl;
    std::cout << std::endl;

    auto logMessage = [] (const std::string& message)
    {
        std::cout << "[Audio] " << message << std::endl;
    };

    std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> player;
    int retryCount = 0;
    const int maxRetries = 10;

    while (retryCount < maxRetries) {
        player = createAudioPlayer(settings, logMessage);

        if (player) {
            std::cout << "Audio interface connected successfully!" << std::endl;
            break;
        }

        retryCount++;
        std::cout << "No audio interfaces available (attempt " << retryCount << "/" << maxRetries << "). Retrying in 5 seconds..." << std::endl;

        if (retryCount >= maxRetries) {
            std::cerr << "Failed to connect to audio interface after " << maxRetries << " attempts." << std::endl;
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << "Audio setup complete:" << std::endl;
    std::cout << "  Sample rate: " << player->options.sampleRate << " Hz" << std::endl;
    std::cout << "  Block size: " << player->options.blockSize << " samples" << std::endl;
    std::cout << "  Output channels: " << player->options.outputChannelCount << std::endl;
    std::cout << std::endl;

    auto audioGenerator = std::make_unique<AudioGenerator>();
    player->addCallback (*audioGenerator);

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

    player->removeCallback (*audioGenerator);
    audioGenerator.reset();

    saveSettings(settings);
    std::cout << "Settings saved." << std::endl;

    return 0;
}