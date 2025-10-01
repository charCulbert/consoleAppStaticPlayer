#pragma once

#include <jack/jack.h>
#include <string>
#include <iostream>

class JackTransportController {
public:
    JackTransportController(const std::string& clientName, double sampleRate);
    ~JackTransportController();

    bool isInitialized() const { return client != nullptr; }
    std::string getErrorMessage() const { return errorMessage; }

    // Update JACK transport position AND state based on audio playback
    void updatePosition(double audioPositionSeconds, bool isPlaying);

    // Command JACK transport to seek to frame 0 and stop (for user stop command)
    void seekToStart();

    // Reset to frame 0 but keep playing (for loop detection)
    void resetToStartAndPlay();

private:
    jack_client_t* client = nullptr;
    double sampleRate;
    std::string errorMessage;
};