#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <signal.h>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>

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

// Signal handler for debugging segfaults
void signal_handler(int sig) {
    void *array[20];
    size_t size;

    std::cerr << "Error: signal " << sig << " caught" << std::endl;

    // Get void*'s for all entries on the stack
    size = backtrace(array, 20);

    // Print out all the frames to stderr
    std::cerr << "Stack trace:" << std::endl;
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    exit(1);
}

// Terminal setup for non-blocking keyboard input
struct TerminalState {
    termios oldSettings;
    bool isSetup = false;
};

TerminalState setupNonBlockingInput() {
    TerminalState state;
    tcgetattr(STDIN_FILENO, &state.oldSettings);

    termios newSettings = state.oldSettings;
    newSettings.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    state.isSetup = true;
    return state;
}

void restoreTerminal(const TerminalState& state) {
    if (state.isSetup) {
        tcsetattr(STDIN_FILENO, TCSANOW, &state.oldSettings);
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    }
}

char getKeyPress() {
    char c = 0;
    read(STDIN_FILENO, &c, 1);
    return c;
}

#define DEBUG_PRINT(msg) do { \
    std::cout << "[DEBUG " << __FILE__ << ":" << __LINE__ << "] " << msg << std::endl; \
    std::cout.flush(); \
} while(0)

struct Settings {
    int sampleRate = 48000;
    int blockSize = 64;
    int outputChannels = 6;
    int inputChannels = 0;
    std::string audioFilePath = "../test_6ch.wav";
    std::string preferredAudioInterface = "";

    bool udpEnabled = true;
    std::string udpAddress = "255.255.255.255";
    int udpPort = 8080;
    std::string udpMessage = "LOOP";
};

std::string getConfigFilePath() {
#ifdef __linux__
    const std::string configDir = "/var/lib/consoleSyncedPlayer";

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
    return "consoleAudioPlayer.config.json";
#endif
}

Settings loadSettings() {
    DEBUG_PRINT("Loading settings...");
    Settings settings;
    const std::string settingsFile = getConfigFilePath();

    try {
        if (std::filesystem::exists(settingsFile)) {
            DEBUG_PRINT("Settings file exists: " << settingsFile);
            auto content = choc::file::loadFileAsString(settingsFile);
            DEBUG_PRINT("File content loaded, size: " << content.size());

            auto json = choc::json::parse(content);
            DEBUG_PRINT("JSON parsed successfully");

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

            DEBUG_PRINT("Settings loaded successfully");
        } else {
            DEBUG_PRINT("Settings file does not exist: " << settingsFile);
        }
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not load settings, using defaults: " << e.what() << std::endl;
    }
    return settings;
}

std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> createAudioPlayer(const Settings& settings, double preferredSampleRate, auto logMessage) {
    DEBUG_PRINT("Creating audio player...");

    std::string preferredDeviceID;
    if (!settings.preferredAudioInterface.empty()) {
        DEBUG_PRINT("Looking for preferred interface: " << settings.preferredAudioInterface);

        // First pass: enumerate with MIDI filtering to avoid memory exhaustion
        choc::audio::io::AudioDeviceOptions enumOptions;

        // Only allow the Raspberry Pi Pico MIDI device
        enumOptions.shouldOpenMIDIInput = [](const std::string& name) {
            return name.find("Pico") != std::string::npos ||
                   name.find("Raspberry Pi") != std::string::npos;
        };
        enumOptions.shouldOpenMIDIOutput = [](const std::string& name) {
            return false; // Don't open any MIDI outputs for now
        };

        auto player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(enumOptions, logMessage);
        DEBUG_PRINT("Created temporary player for device enumeration");

        auto devices = player->getAvailableOutputDevices();
        DEBUG_PRINT("Got " << devices.size() << " available output devices");

        std::cout << "\nSearching for audio interface containing: \"" << settings.preferredAudioInterface << "\"" << std::endl;
        std::cout << "Available output devices:" << std::endl;

        for (const auto& device : devices) {
            std::cout << "  - " << device.name << " (ID: " << device.deviceID << ")" << std::endl;

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

    DEBUG_PRINT("Setting up audio device options");
    choc::audio::io::AudioDeviceOptions options;
    options.sampleRate = preferredSampleRate;
    options.blockSize = settings.blockSize;
    options.outputChannelCount = settings.outputChannels;
    options.inputChannelCount = settings.inputChannels;
    options.outputDeviceID = preferredDeviceID;

    // Filter MIDI devices for the actual player too
    options.shouldOpenMIDIInput = [](const std::string& name) {
        // Only open Raspberry Pi Pico MIDI
        return name.find("Pico") != std::string::npos ||
               name.find("Raspberry Pi") != std::string::npos;
    };

    options.shouldOpenMIDIOutput = [](const std::string& name) {
        return false; // No MIDI output needed
    };

    DEBUG_PRINT("Creating final audio player with options");
    auto player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(options, logMessage);
    DEBUG_PRINT("Audio player created");

    bool gotPreferredRate = player->getLastError().empty() &&
                           std::abs(player->options.sampleRate - preferredSampleRate) < 0.1;

    if (!gotPreferredRate && preferredSampleRate != settings.sampleRate) {
        std::cout << "Device doesn't support " << preferredSampleRate << " Hz (got "
                  << player->options.sampleRate << " Hz), trying " << settings.sampleRate << " Hz..." << std::endl;

        options.sampleRate = settings.sampleRate;
        DEBUG_PRINT("Retrying with fallback sample rate");
        player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer>(options, logMessage);
        if (!player->getLastError().empty()) return nullptr;
    }

    DEBUG_PRINT("Audio player setup complete");
    return player;
}

double getAudioFileSampleRate(const std::string& filePath) {
    DEBUG_PRINT("Getting audio file sample rate for: " << filePath);
    try {
        auto fileStream = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!fileStream || !fileStream->is_open()) {
            DEBUG_PRINT("Could not open file");
            return 0.0;
        }

        choc::audio::AudioFileFormatList formatList;
        formatList.addFormat<choc::audio::WAVAudioFileFormat<false>>();

        auto fileReader = formatList.createReader(fileStream);
        if (!fileReader) {
            DEBUG_PRINT("Could not create file reader");
            return 0.0;
        }

        double rate = fileReader->getProperties().sampleRate;
        DEBUG_PRINT("File sample rate: " << rate);
        return rate;
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not read file sample rate: " << e.what() << std::endl;
        return 0.0;
    }
}

int main()
{
    // Install signal handlers for debugging
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);

    DEBUG_PRINT("Starting CHOC Audio File Player");

    std::cout << "CHOC Audio File Player Example" << std::endl;
    std::cout << "==============================" << std::endl;

    auto settings = loadSettings();

    std::cout << "\nLoaded settings:" << std::endl;
    std::cout << "  Sample rate: " << settings.sampleRate << " Hz" << std::endl;
    std::cout << "  Block size: " << settings.blockSize << " samples" << std::endl;
    std::cout << "  Output channels: " << settings.outputChannels << std::endl;
    std::cout << "  Audio file path: " << settings.audioFilePath << std::endl << std::endl;

    auto logMessage = [] (const std::string& message) {
        std::cout << "[Audio] " << message << std::endl;
        std::cout.flush();
    };

    if (!std::filesystem::exists(settings.audioFilePath)) {
        std::cerr << "Error: Audio file not found at " << settings.audioFilePath << std::endl;
        return 1;
    }

    DEBUG_PRINT("Audio file exists");

    double fileSampleRate = getAudioFileSampleRate(settings.audioFilePath);
    double preferredSampleRate = (fileSampleRate > 0) ? fileSampleRate : settings.sampleRate;

    std::cout << "  File sample rate: " << fileSampleRate << " Hz" << std::endl;
    std::cout << "  Trying to open audio device at: " << preferredSampleRate << " Hz" << std::endl << std::endl;

    std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> player;
    for (int retryCount = 0; ; ++retryCount) {
        DEBUG_PRINT("Audio player creation attempt " << retryCount);
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

    DEBUG_PRINT("Creating BufferedAudioFilePlayer");
    auto audioFilePlayer = std::make_unique<BufferedAudioFilePlayer>(settings.audioFilePath, player->options.sampleRate);
    DEBUG_PRINT("BufferedAudioFilePlayer created");

    if (!audioFilePlayer->isLoaded()) {
        std::cerr << "Error loading audio file: " << audioFilePlayer->getErrorMessage() << std::endl;
        return 1;
    }

    DEBUG_PRINT("Audio file loaded successfully");

    // Pre-fill buffer before starting audio callbacks
    DEBUG_PRINT("Starting playback");
    audioFilePlayer->startPlayback();
    DEBUG_PRINT("Playback started");

    // Setup UDP sender if enabled
    std::unique_ptr<UdpSender> udpSender;
    std::atomic<bool> syncThreadRunning{false};
    std::thread syncSenderThread;

    if (settings.udpEnabled) {
        DEBUG_PRINT("Creating UDP sender");
        udpSender = std::make_unique<UdpSender>(settings.udpAddress, settings.udpPort);
        std::cout << "UDP messaging enabled - target: " << settings.udpAddress << ":" << settings.udpPort
                  << " message: \"" << settings.udpMessage << "\"" << std::endl;
        DEBUG_PRINT("UDP sender created");

        // Start dedicated 1kHz sync sender thread (audio-clock driven, non-blocking)
        syncThreadRunning = true;
        syncSenderThread = std::thread([&]() {
            DEBUG_PRINT("Sync sender thread started at 1kHz");
            while (syncThreadRunning.load(std::memory_order_relaxed)) {
                // Read audio position (lock-free atomic read - fast!)
                double position = audioFilePlayer->getCurrentAudioPosition();

                // Send SYNC message with current audio timestamp
                std::string syncMsg = "SYNC " + std::to_string(position);
                udpSender->send(syncMsg);

                // 1kHz = 1ms interval
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            DEBUG_PRINT("Sync sender thread stopped");
        });

        std::cout << "Audio clock sync: broadcasting at 1kHz (1ms intervals)" << std::endl;
    }

    std::cout << "Adding audio callback..." << std::endl;
    DEBUG_PRINT("About to add callback");
    player->addCallback(*audioFilePlayer);
    DEBUG_PRINT("Callback added");

    std::cout << "Playing file: " << settings.audioFilePath << "..." << std::endl;

    // Setup keyboard input
    auto termState = setupNonBlockingInput();

    // Send initial PLAY command
    if (udpSender) {
        if (udpSender->send("PLAY")) {
            std::cout << "UDP: Sent PLAY command" << std::endl;
        }
    }

    std::cout << "\nKeyboard controls:" << std::endl;
    std::cout << "  SPACE - Pause/Resume" << std::endl;
    std::cout << "  S     - Stop and reset to beginning" << std::endl;
    std::cout << "  Q     - Quit" << std::endl << std::endl;

    bool running = true;
    DEBUG_PRINT("Entering main playback loop");
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Check for keyboard input
        char key = getKeyPress();
        if (key != 0) {
            switch (key) {
                case ' ': // Space - toggle pause/play
                    if (audioFilePlayer->isStillPlaying()) {
                        audioFilePlayer->pause();
                        std::cout << "⏸  Paused" << std::endl;
                        if (udpSender) udpSender->send("PAUSE");
                    } else {
                        audioFilePlayer->play();
                        std::cout << "▶  Playing" << std::endl;
                        if (udpSender) udpSender->send("PLAY");
                    }
                    break;

                case 's':
                case 'S':
                    audioFilePlayer->stop();
                    std::cout << "⏹  Stopped" << std::endl;
                    if (udpSender) udpSender->send("STOP");
                    break;

                case 'q':
                case 'Q':
                    std::cout << "Quitting..." << std::endl;
                    running = false;
                    break;
            }
        }

        // Check for loop detection and send UDP message
        if (udpSender && audioFilePlayer->getLoopPlaybackDetected()) {
            if (udpSender->send("SEEK 0")) {
                std::cout << "↻  Loop detected - sent SEEK 0 to video player" << std::endl;
            } else {
                std::cout << "Failed to send UDP message" << std::endl;
            }
        }

        // Monitor buffer health
        static int reportCount = 0;
        if (reportCount++ % 10000 == 0) // Every 10 seconds
        {
            uint32_t used = audioFilePlayer->getBufferUsedSlots();
            uint32_t total = audioFilePlayer->getBufferSize();
            double percentage = (double)used / total * 100.0;
            std::cout << "Buffer: " << used << "/" << total << " ("
                     << std::fixed << std::setprecision(1) << percentage << "%)" << std::endl;
        }
    }

    std::cout << "\nPlayback finished." << std::endl;

    // Stop sync sender thread first
    if (syncThreadRunning.load()) {
        DEBUG_PRINT("Stopping sync sender thread");
        syncThreadRunning.store(false, std::memory_order_relaxed);
        if (syncSenderThread.joinable()) {
            syncSenderThread.join();
        }
    }

    // Send STOP to video player
    if (udpSender) {
        udpSender->send("STOP");
    }

    // Restore terminal
    restoreTerminal(termState);

    DEBUG_PRINT("Removing callback");
    player->removeCallback(*audioFilePlayer);
    DEBUG_PRINT("Program ending normally");

    return 0;
}
