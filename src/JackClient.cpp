#include "../include/JackClient.h"
#include "choc/audio/choc_AudioFileFormat.h"
#include <iostream>
#include <thread>
#include <chrono>

JackClient::JackClient(const std::string& clientName) {
    jack_status_t status;
    client = jack_client_open(clientName.c_str(), JackNullOption, &status);

    if (!client) {
        std::cerr << "Failed to open JACK client" << std::endl;
        return;
    }

    sampleRate = jack_get_sample_rate(client);
    blockSize = jack_get_buffer_size(client);

    std::cout << "JACK: " << sampleRate << " Hz, block size " << blockSize << std::endl;
}

JackClient::~JackClient() {
    shutdown();
}

bool JackClient::initialize(int numChannels, BufferedAudioFilePlayer* player, uint64_t fileTotalFrames) {
    if (!client) return false;

    this->numOutputChannels = numChannels;
    this->audioPlayer = player;
    this->fileTotalFrames = fileTotalFrames;

    // Create output ports
    outputPorts = new jack_port_t*[numChannels];
    for (int ch = 0; ch < numChannels; ch++) {
        std::string portName = "output_" + std::to_string(ch + 1);
        outputPorts[ch] = jack_port_register(client, portName.c_str(),
                                              JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!outputPorts[ch]) {
            std::cerr << "Failed to register JACK output port " << ch << std::endl;
            return false;
        }
    }

    // Register process callback
    if (jack_set_process_callback(client, processCallback, this) != 0) {
        std::cerr << "Failed to set JACK process callback" << std::endl;
        return false;
    }

    // Register as timebase master
    if (jack_set_timebase_callback(client, 0, timebaseCallback, this) != 0) {
        std::cerr << "Failed to set JACK timebase callback" << std::endl;
        return false;
    }

    return true;
}

bool JackClient::activate() {
    if (!client) return false;

    if (jack_activate(client) != 0) {
        std::cerr << "Failed to activate JACK client" << std::endl;
        return false;
    }

    // Auto-connect to system playback ports
    const char** systemPorts = jack_get_ports(client, "system:playback_", nullptr, JackPortIsInput);
    if (systemPorts) {
        for (int ch = 0; ch < numOutputChannels && systemPorts[ch]; ch++) {
            std::string ourPort = "consoleAudioPlayer:output_" + std::to_string(ch + 1);
            jack_connect(client, ourPort.c_str(), systemPorts[ch]);
        }
        jack_free(systemPorts);
    }

    // Start transport
    jack_transport_start(client);

    return true;
}

void JackClient::shutdown() {
    if (client) {
        // Trigger fade-out over 50ms
        shuttingDown.store(true, std::memory_order_release);

        const int fadeSteps = 50;
        const int fadeStepMs = 1;
        for (int i = 0; i < fadeSteps; i++) {
            float fade = 1.0f - (float)(i + 1) / fadeSteps;
            fadeMultiplier.store(fade, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(fadeStepMs));
        }

        jack_deactivate(client);
        jack_client_close(client);
        client = nullptr;
    }

    if (outputPorts) {
        delete[] outputPorts;
        outputPorts = nullptr;
    }
}

int JackClient::processCallback(jack_nframes_t nframes, void* arg) {
    auto* self = static_cast<JackClient*>(arg);
    if (!self || !self->audioPlayer) return 0;

    // Get JACK output buffers
    float* outputBuffers[self->numOutputChannels];
    for (int ch = 0; ch < self->numOutputChannels; ch++) {
        outputBuffers[ch] = static_cast<float*>(jack_port_get_buffer(self->outputPorts[ch], nframes));
    }

    // Wrap in CHOC view
    auto outputView = choc::buffer::createChannelArrayView(outputBuffers,
                                                            (choc::buffer::ChannelCount)self->numOutputChannels,
                                                            (choc::buffer::FrameCount)nframes);

    // Process audio
    self->audioPlayer->processBlock(outputView);

    // Apply fade-out if shutting down
    if (self->shuttingDown.load(std::memory_order_relaxed)) {
        float fade = self->fadeMultiplier.load(std::memory_order_relaxed);
        for (int ch = 0; ch < self->numOutputChannels; ch++) {
            for (jack_nframes_t i = 0; i < nframes; i++) {
                outputBuffers[ch][i] *= fade;
            }
        }
    }

    // Update playback position (single source of truth)
    int64_t currentPos = self->playbackPosition.load(std::memory_order_relaxed);
    int64_t newPos = currentPos + nframes;

    // Wrap at file end
    if (newPos >= (int64_t)self->fileTotalFrames) {
        newPos = newPos % self->fileTotalFrames;
    }

    self->playbackPosition.store(newPos, std::memory_order_release);

    return 0;
}

void JackClient::timebaseCallback(jack_transport_state_t state, jack_nframes_t nframes,
                                  jack_position_t* pos, int new_pos, void* arg) {
    auto* self = static_cast<JackClient*>(arg);
    if (!self) return;

    // Report our playback position to JACK Transport
    int64_t currentPos = self->playbackPosition.load(std::memory_order_acquire);

    pos->frame = currentPos;
    pos->valid = (jack_position_bits_t)0;  // Only provide frame count
}
