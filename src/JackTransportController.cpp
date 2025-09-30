#include "../include/JackTransportController.h"

#define DEBUG_PRINT(msg) do { \
    std::cout << "[JackTransport] " << msg << std::endl; \
    std::cout.flush(); \
} while(0)

JackTransportController::JackTransportController(const std::string& clientName, double sr)
    : sampleRate(sr)
{
    jack_status_t status;
    client = jack_client_open(clientName.c_str(), JackNullOption, &status);

    if (!client) {
        errorMessage = "Failed to open JACK client";
        DEBUG_PRINT(errorMessage);
        return;
    }

    if (status & JackServerStarted) {
        DEBUG_PRINT("JACK server started");
    }

    if (status & JackNameNotUnique) {
        std::string actualName = jack_get_client_name(client);
        DEBUG_PRINT("JACK client name '" << clientName << "' was taken, using '" << actualName << "'");
    }

    // Activate the client (transport control doesn't require audio callbacks)
    if (jack_activate(client)) {
        errorMessage = "Cannot activate JACK client";
        DEBUG_PRINT(errorMessage);
        jack_client_close(client);
        client = nullptr;
        return;
    }

    DEBUG_PRINT("JACK transport controller initialized successfully");
}

JackTransportController::~JackTransportController() {
    if (client) {
        jack_client_close(client);
        client = nullptr;
        DEBUG_PRINT("JACK client closed");
    }
}

void JackTransportController::updatePosition(double audioPositionSeconds, bool isPlaying) {
    if (!client) return;

    // Convert audio position to JACK frames
    jack_nframes_t targetFrame = (jack_nframes_t)(audioPositionSeconds * sampleRate);

    // Update transport position
    jack_transport_locate(client, targetFrame);

    // Set transport state to match audio playback state
    if (isPlaying) {
        jack_transport_start(client);
    } else {
        jack_transport_stop(client);
    }
}

void JackTransportController::seekToStart() {
    if (!client) return;

    // Locate transport to frame 0
    jack_transport_locate(client, 0);

    // Stop transport (user pressed stop)
    jack_transport_stop(client);

    DEBUG_PRINT("⏹  JACK transport reset to frame 0 and stopped");
}

void JackTransportController::resetToStartAndPlay() {
    if (!client) return;

    // Locate transport to frame 0
    jack_transport_locate(client, 0);

    // Restart transport immediately (seamless loop)
    jack_transport_start(client);

    DEBUG_PRINT("↻  JACK transport looped to frame 0 (playing)");
}