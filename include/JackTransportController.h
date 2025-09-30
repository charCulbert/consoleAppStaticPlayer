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

    // Update JACK transport position based on audio playback (call this frequently!)
    void updatePosition(double audioPositionSeconds);

    // Command JACK transport to seek to frame 0
    void seekToStart();

private:
    jack_client_t* client = nullptr;
    double sampleRate;
    std::string errorMessage;
};