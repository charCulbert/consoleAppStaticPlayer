#pragma once

#include <jack/jack.h>
#include <jack/transport.h>
#include <atomic>
#include <memory>
#include <string>
#include "BufferedAudioFilePlayer.h"

class JackClient {
public:
    JackClient(const std::string& clientName = "consoleAudioPlayer");
    ~JackClient();

    bool initialize(int numOutputChannels, BufferedAudioFilePlayer* player, uint64_t fileTotalFrames);
    bool activate();
    void shutdown();

    jack_nframes_t getSampleRate() const { return sampleRate; }
    jack_nframes_t getBlockSize() const { return blockSize; }

    // Atomic playback position (single source of truth)
    std::atomic<int64_t> playbackPosition{0};

    // Fade-out on shutdown
    std::atomic<bool> shuttingDown{false};
    std::atomic<float> fadeMultiplier{1.0f};

private:
    jack_client_t* client = nullptr;
    jack_port_t** outputPorts = nullptr;
    int numOutputChannels = 0;
    jack_nframes_t sampleRate = 0;
    jack_nframes_t blockSize = 0;

    BufferedAudioFilePlayer* audioPlayer = nullptr;
    uint64_t fileTotalFrames = 0;  // For wrapping playback position

    // JACK callbacks (static, forward to instance methods)
    static int processCallback(jack_nframes_t nframes, void* arg);
    static void timebaseCallback(jack_transport_state_t state, jack_nframes_t nframes,
                                 jack_position_t* pos, int new_pos, void* arg);
};
