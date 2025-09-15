#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>

#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/text/choc_JSON.h"
#include "choc/text/choc_Files.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "BufferedAudioFilePlayer.h"

struct Settings {
    int sampleRate = 48000;
    int blockSize = 64;
    int outputChannels = 6;
    int inputChannels = 0;
    std::string audioFilePath = "../test_6ch.wav"; // Use the sine wave file for testing!
};

Settings loadSettings() {
    Settings settings;
    const std::string settingsFile = "audio_settings.json";

    try {
        if (std::filesystem::exists(settingsFile)) {
            auto content = choc::file::loadFileAsString(settingsFile);
            auto json = choc::json::parse(content);

            settings.sampleRate     = json["sampleRate"]    .getWithDefault<int>(settings.sampleRate);
            settings.blockSize      = json["blockSize"]     .getWithDefault<int>(settings.blockSize);
            settings.outputChannels = json["outputChannels"].getWithDefault<int>(settings.outputChannels);
            settings.inputChannels  = json["inputChannels"] .getWithDefault<int>(settings.inputChannels);
            settings.audioFilePath  = json["audioFilePath"] .getWithDefault<std::string>(settings.audioFilePath);
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
            "inputChannels", settings.inputChannels,
            "audioFilePath", settings.audioFilePath
        );
        choc::file::replaceFileWithContent("audio_settings.json", choc::json::toString(json, true));
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not save settings: " << e.what() << std::endl;
    }
}

std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> createAudioPlayer(const Settings& settings, double preferredSampleRate, auto logMessage) {
    // First try preferred sample rate
    choc::audio::io::AudioDeviceOptions options;
    options.sampleRate = preferredSampleRate;
    options.blockSize = settings.blockSize;
    options.outputChannelCount = settings.outputChannels;
    options.inputChannelCount = settings.inputChannels;

    auto player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(options, logMessage);

    // Check if we actually got the preferred rate (not just no error)
    bool gotPreferredRate = player->getLastError().empty() &&
                           std::abs(player->options.sampleRate - preferredSampleRate) < 0.1;

    if (!gotPreferredRate && preferredSampleRate != settings.sampleRate) {
        std::cout << "Device doesn't support " << preferredSampleRate << " Hz (got "
                  << player->options.sampleRate << " Hz), trying " << settings.sampleRate << " Hz..." << std::endl;

        // Try fallback sample rate
        options.sampleRate = settings.sampleRate;
        player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(options, logMessage);
        if (!player->getLastError().empty()) return nullptr;
    }

    return player;
}

double getAudioFileSampleRate(const std::string& filePath) {
    try {
        auto fileStream = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!fileStream || !fileStream->is_open()) {
            return 0.0;
        }

        choc::audio::AudioFileFormatList formatList;
        formatList.addFormat<choc::audio::WAVAudioFileFormat<false>>();

        auto fileReader = formatList.createReader(fileStream);
        if (!fileReader) {
            return 0.0;
        }

        return fileReader->getProperties().sampleRate;
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not read file sample rate: " << e.what() << std::endl;
        return 0.0;
    }
}

int main()
{
    std::cout << "CHOC Audio File Player Example" << std::endl;
    std::cout << "==============================" << std::endl;

    auto settings = loadSettings();

    std::cout << "\nLoaded settings:" << std::endl;
    std::cout << "  Sample rate: " << settings.sampleRate << " Hz" << std::endl;
    std::cout << "  Block size: " << settings.blockSize << " samples" << std::endl;
    std::cout << "  Output channels: " << settings.outputChannels << std::endl;
    std::cout << "  Audio file path: " << settings.audioFilePath << std::endl << std::endl;

    auto logMessage = [] (const std::string& message) { std::cout << "[Audio] " << message << std::endl; };

    if (!std::filesystem::exists(settings.audioFilePath)) {
        std::cerr << "Error: Audio file not found at " << settings.audioFilePath << std::endl;
        return 1;
    }

    // Get the file's sample rate to try matching the audio device
    double fileSampleRate = getAudioFileSampleRate(settings.audioFilePath);
    double preferredSampleRate = (fileSampleRate > 0) ? fileSampleRate : settings.sampleRate;

    std::cout << "  File sample rate: " << fileSampleRate << " Hz" << std::endl;
    std::cout << "  Trying to open audio device at: " << preferredSampleRate << " Hz" << std::endl << std::endl;

    std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> player;
    for (int retryCount = 0; ; ++retryCount) {
        player = createAudioPlayer(settings, preferredSampleRate, logMessage);
        if (player) break;
        if (retryCount >= 5) {
            std::cerr << "Failed to connect to audio interface after " << retryCount << " attempts." << std::endl;
            return 1;
        }
        std::cout << "No audio interfaces available. Retrying in 3 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "\nAudio setup complete:" << std::endl;
    std::cout << "  Actual Sample rate: " << player->options.sampleRate << " Hz" << std::endl;
    std::cout << "  Actual Block size: " << player->options.blockSize << " samples" << std::endl;
    std::cout << "  Actual Output channels: " << player->options.outputChannelCount << std::endl << std::endl;

    auto audioFilePlayer = std::make_unique<BufferedAudioFilePlayer>(settings.audioFilePath, player->options.sampleRate);

    if (!audioFilePlayer->isLoaded()) {
        std::cerr << "Error loading audio file: " << audioFilePlayer->getErrorMessage() << std::endl;
        return 1;
    }

    // Pre-fill buffer before starting audio callbacks
    audioFilePlayer->startPlayback();

    std::cout << "Adding audio callback..." << std::endl;
    player->addCallback(*audioFilePlayer);
    std::cout << "Playing file: " << settings.audioFilePath << "..." << std::endl;

    while (audioFilePlayer->isStillPlaying()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Monitor buffer health
        static int reportCount = 0;
        if (reportCount++ % 20 == 0) // Every 2 seconds
        {
            uint32_t used = audioFilePlayer->getBufferUsedSlots();
            uint32_t total = audioFilePlayer->getBufferSize();
            double percentage = (double)used / total * 100.0;
            std::cout << "Buffer: " << used << "/" << total << " ("
                     << std::fixed << std::setprecision(1) << percentage << "%)" << std::endl;
        }

        // // Add timeout for testing
        // static auto startTime = std::chrono::steady_clock::now();
        // auto elapsed = std::chrono::steady_clock::now() - startTime;
        // if (elapsed > std::chrono::seconds(15)) {
        //     std::cout << "Test timeout reached, stopping..." << std::endl;
        //     break;
        // }
    }

    std::cout << "\nPlayback finished." << std::endl;

    player->removeCallback(*audioFilePlayer);
    saveSettings(settings);
    std::cout << "Settings saved." << std::endl;

    return 0;
}