#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <algorithm>

#ifdef __linux__
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#endif

#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/text/choc_JSON.h"
#include "choc/text/choc_Files.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "BufferedAudioFilePlayer.h"
#include "UdpSender.h"

struct Settings {
    int sampleRate = 48000;
    int blockSize = 64;
    int outputChannels = 6;
    int inputChannels = 0;
    std::string audioFilePath = "../test_6ch.wav"; // Use the sine wave file for testing!
    std::string preferredAudioInterface = ""; // Empty = use default, otherwise search for matching interface name

    // UDP messaging settings
    bool udpEnabled = true;
    std::string udpAddress = "255.255.255.255"; // Broadcast by default
    int udpPort = 8080;
    std::string udpMessage = "LOOP";
};

std::string getConfigFilePath() {
#ifdef __linux__
    // For Yocto/Linux: use /var/lib/consoleAudioPlayer/ for writable config
    const std::string configDir = "/var/lib/consoleAudioPlayer";

    // Create directory if it doesn't exist
    try {
        if (!std::filesystem::exists(configDir)) {
            std::filesystem::create_directories(configDir);
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: Could not create config directory " << configDir
                  << ", falling back to current directory: " << e.what() << std::endl;
        return "consoleAudioPlayer.config.json";
    }

    return configDir + "/consoleAudioPlayer.config.json";
#else
    // For macOS/Windows: use current working directory
    return "consoleAudioPlayer.config.json";
#endif
}

Settings loadSettings() {
    Settings settings;
    const std::string settingsFile = getConfigFilePath();

    try {
        if (std::filesystem::exists(settingsFile)) {
            auto content = choc::file::loadFileAsString(settingsFile);
            auto json = choc::json::parse(content);

            settings.sampleRate     = json["sampleRate"]    .getWithDefault<int>(settings.sampleRate);
            settings.blockSize      = json["blockSize"]     .getWithDefault<int>(settings.blockSize);
            settings.outputChannels = json["outputChannels"].getWithDefault<int>(settings.outputChannels);
            settings.inputChannels  = json["inputChannels"] .getWithDefault<int>(settings.inputChannels);
            settings.audioFilePath  = json["audioFilePath"] .getWithDefault<std::string>(settings.audioFilePath);
            settings.preferredAudioInterface = json["preferredAudioInterface"].getWithDefault<std::string>(settings.preferredAudioInterface);

            settings.udpEnabled     = json["udpEnabled"]    .getWithDefault<bool>(settings.udpEnabled);
            settings.udpAddress     = json["udpAddress"]    .getWithDefault<std::string>(settings.udpAddress);
            settings.udpPort        = json["udpPort"]       .getWithDefault<int>(settings.udpPort);
            settings.udpMessage     = json["udpMessage"]    .getWithDefault<std::string>(settings.udpMessage);
        }
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not load settings, using defaults: " << e.what() << std::endl;
    }
    return settings;
}


std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> createAudioPlayer(const Settings& settings, double preferredSampleRate, auto logMessage) {
    // Try to find preferred audio interface if specified
    std::string preferredDeviceID;
    if (!settings.preferredAudioInterface.empty()) {
        auto player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(choc::audio::io::AudioDeviceOptions{}, logMessage);
        auto devices = player->getAvailableOutputDevices();

        std::cout << "\nSearching for audio interface containing: \"" << settings.preferredAudioInterface << "\"" << std::endl;
        std::cout << "Available output devices:" << std::endl;

        for (const auto& device : devices) {
            std::cout << "  - " << device.name << " (ID: " << device.deviceID << ")" << std::endl;

            // Case-insensitive search for the preferred name in device name
            std::string deviceNameLower = device.name;
            std::string preferredLower = settings.preferredAudioInterface;
            std::transform(deviceNameLower.begin(), deviceNameLower.end(), deviceNameLower.begin(), ::tolower);
            std::transform(preferredLower.begin(), preferredLower.end(), preferredLower.begin(), ::tolower);

            if (deviceNameLower.find(preferredLower) != std::string::npos) {
                preferredDeviceID = device.deviceID;
                std::cout << "  -> Found matching device: " << device.name << std::endl;
                break;
            }
        }

        if (preferredDeviceID.empty()) {
            std::cout << "  -> No matching device found, using default" << std::endl;
        }
    }

    // Set up audio device options
    choc::audio::io::AudioDeviceOptions options;
    options.sampleRate = preferredSampleRate;
    options.blockSize = settings.blockSize;
    options.outputChannelCount = settings.outputChannels;
    options.inputChannelCount = settings.inputChannels;
    options.outputDeviceID = preferredDeviceID; // Use found device or empty for default

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

    // Setup UDP sender if enabled
    std::unique_ptr<UdpSender> udpSender;
    if (settings.udpEnabled) {
        udpSender = std::make_unique<UdpSender>(settings.udpAddress, settings.udpPort);
        std::cout << "UDP messaging enabled - target: " << settings.udpAddress << ":" << settings.udpPort
                  << " message: \"" << settings.udpMessage << "\"" << std::endl;
    }

    std::cout << "Adding audio callback..." << std::endl;
    player->addCallback(*audioFilePlayer);
    std::cout << "Playing file: " << settings.audioFilePath << "..." << std::endl;

    while (audioFilePlayer->isStillPlaying()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check for loop detection and send UDP message
        if (udpSender && audioFilePlayer->getLoopPlaybackDetected()) {
            if (udpSender->send(settings.udpMessage)) {
                std::cout << "UDP message sent: \"" << settings.udpMessage << "\" to "
                          << settings.udpAddress << ":" << settings.udpPort << std::endl;
            } else {
                std::cout << "Failed to send UDP message" << std::endl;
            }
        }

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

    return 0;
}