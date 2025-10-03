#include <iostream>
#include <csignal>
#include <filesystem>
#include <thread>
#include <chrono>
#include "BufferedAudioFilePlayer.h"
#include "JackClient.h"

static volatile bool running = true;

void signalHandler(int signal) {
    running = false;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file>" << std::endl;
        return 1;
    }

    std::string audioFilePath = argv[1];

    if (!std::filesystem::exists(audioFilePath)) {
        std::cerr << "Error: File not found: " << audioFilePath << std::endl;
        return 1;
    }

    std::cout << "consoleAudioPlayer" << std::endl;

    // Create JACK client
    JackClient jackClient;
    if (jackClient.getSampleRate() == 0) {
        std::cerr << "Error: JACK server not running" << std::endl;
        return 1;
    }

    // Create audio file player
    BufferedAudioFilePlayer player(audioFilePath, jackClient.getSampleRate());
    if (!player.isLoaded()) {
        std::cerr << "Error: " << player.getErrorMessage() << std::endl;
        return 1;
    }

    // Calculate file duration in JACK sample rate (for position wrapping)
    double fileDuration = (double)player.getTotalFrames() / player.getFileSampleRate();
    uint64_t fileDurationFrames = (uint64_t)(fileDuration * jackClient.getSampleRate());

    // Initialize JACK client with player
    if (!jackClient.initialize(player.getNumChannels(), &player, fileDurationFrames)) {
        std::cerr << "Error: Failed to initialize JACK client" << std::endl;
        return 1;
    }

    // Activate JACK and start transport
    if (!jackClient.activate()) {
        std::cerr << "Error: Failed to activate JACK client" << std::endl;
        return 1;
    }

    std::cout << "Playing (Ctrl+C to quit)" << std::endl;

    // Setup signal handler for clean exit
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Main loop
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down..." << std::endl;

    // Explicitly shut down JACK before player destructor runs
    jackClient.shutdown();

    return 0;
}
