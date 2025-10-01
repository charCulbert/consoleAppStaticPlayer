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

#include "choc/text/choc_JSON.h"
#include "choc/text/choc_Files.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_AudioSampleData.h"
#include "BufferedAudioFilePlayer.h"
#include <jack/jack.h>
#include <jack/transport.h>
#include <jack/midiport.h>

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
    const std::string configName = "consoleAudioPlayer.config.json";

    // Priority order: /var/lib/consolePlayers/ -> ../ -> ./
    std::vector<std::string> searchPaths = {
#ifdef __linux__
        "/var/lib/consolePlayers/" + configName,
#endif
        "../" + configName,
        configName
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    // If none exist, return the first path (will use defaults)
    return searchPaths[0];
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

        double rate = fileReader->getProperties().sampleRate;
        return rate;
    } catch (const std::exception& e) {
        std::cout << "Warning: Could not read file sample rate: " << e.what() << std::endl;
        return 0.0;
    }
}

// Global context for JACK callback
struct JackAudioContext {
    BufferedAudioFilePlayer* audioPlayer = nullptr;
    jack_port_t** outputPorts = nullptr;
    int numOutputChannels = 0;
    jack_client_t* client = nullptr;
    uint64_t fileDurationFrames = 0;  // File duration in output sample rate
    std::atomic<uint64_t> lastKnownPosition{0};  // Cached position from file
    jack_port_t* midiInputPort = nullptr;  // MIDI input for control

    // Transport control flags (set in audio callback, handled in main thread)
    std::atomic<bool> requestPlay{false};
    std::atomic<bool> requestStop{false};
};

// JACK audio process callback - runs in realtime thread
int jackProcessCallback(jack_nframes_t nframes, void* arg) {
    auto* ctx = static_cast<JackAudioContext*>(arg);
    if (!ctx || !ctx->audioPlayer) return 0;

    // Handle MIDI input (if port exists)
    if (ctx->midiInputPort) {
        void* midiBuffer = jack_port_get_buffer(ctx->midiInputPort, nframes);
        jack_nframes_t eventCount = jack_midi_get_event_count(midiBuffer);

        for (jack_nframes_t i = 0; i < eventCount; i++) {
            jack_midi_event_t event;
            if (jack_midi_event_get(&event, midiBuffer, i) == 0) {
                // Parse MIDI CC messages (status byte 0xB0-0xBF)
                if (event.size >= 3 && (event.buffer[0] & 0xF0) == 0xB0) {
                    uint8_t ccNumber = event.buffer[1];
                    uint8_t ccValue = event.buffer[2];
                    float normalizedValue = ccValue / 127.0f;

                    // Debug MIDI (disabled - enable if debugging MIDI issues)
                    // printf("[MIDI] CC%d = %d (%.2f)\n", ccNumber, ccValue, normalizedValue);

                    // Handle CC1, CC2, CC3
                    switch (ccNumber) {
                        case 1: // Play
                            if (normalizedValue > 0.5f) {
                                ctx->requestPlay.store(true, std::memory_order_release);
                            }
                            break;
                        case 2: // Stop/Pause (toggle behavior)
                            if (normalizedValue > 0.5f) {
                                // If playing -> pause, if paused -> stop and reset
                                if (ctx->audioPlayer->isStillPlaying()) {
                                    ctx->audioPlayer->pause();
                                    jack_transport_stop(ctx->client);
                                } else {
                                    ctx->requestStop.store(true, std::memory_order_release);
                                }
                            }
                            break;
                        case 3: // Volume
                            ctx->audioPlayer->setGain(normalizedValue);
                            break;
                    }
                }
            }
        }
    }

    // Get JACK output buffers (raw float* pointers)
    float* outputBuffers[ctx->numOutputChannels];
    for (int ch = 0; ch < ctx->numOutputChannels; ch++) {
        outputBuffers[ch] = static_cast<float*>(jack_port_get_buffer(ctx->outputPorts[ch], nframes));
    }

    // Wrap JACK buffers in CHOC's BufferView (zero-copy)
    auto outputView = choc::buffer::createChannelArrayView(outputBuffers,
                                                            (choc::buffer::ChannelCount)ctx->numOutputChannels,
                                                            (choc::buffer::FrameCount)nframes);

    // Call our audio processing
    ctx->audioPlayer->processBlock(outputView);

    // Cache current position for timebase callback (derived from fileReadPosition)
    ctx->lastKnownPosition.store(ctx->audioPlayer->getCurrentOutputFrame(), std::memory_order_release);

    return 0;
}

// JACK timebase callback - called after process callback to update position
// As timebase master, we write our current audio position to JACK Transport
void jackTimebaseCallback(jack_transport_state_t state, jack_nframes_t nframes,
                          jack_position_t *pos, int new_pos, void *arg) {
    auto* ctx = static_cast<JackAudioContext*>(arg);
    if (!ctx || !ctx->audioPlayer) return;

    // If stop was requested, force position to 0 and don't update from audio
    if (ctx->requestStop.load(std::memory_order_acquire)) {
        pos->frame = 0;
        pos->valid = (jack_position_bits_t)0;
        return;
    }

    // Use cached position (updated by process callback) - safe even during seeks
    uint64_t currentAudioFrame = ctx->lastKnownPosition.load(std::memory_order_acquire);

    // Auto-wrap at file end
    if (currentAudioFrame >= ctx->fileDurationFrames) {
        currentAudioFrame = currentAudioFrame % ctx->fileDurationFrames;
    }

    pos->frame = currentAudioFrame;  // Write audio position to JACK (master controls timeline)
    pos->valid = (jack_position_bits_t)0; // We only provide frame count, no BBT/timecode
}

int main()
{
    // Install signal handlers for debugging
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);

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

    double fileSampleRate = getAudioFileSampleRate(settings.audioFilePath);
    double preferredSampleRate = (fileSampleRate > 0) ? fileSampleRate : settings.sampleRate;

    std::cout << "  File sample rate: " << fileSampleRate << " Hz" << std::endl << std::endl;

    // Initialize JACK client
    jack_status_t jackStatus;
    jack_client_t* jackClient = jack_client_open("consoleAudioPlayer", JackNullOption, &jackStatus);

    if (!jackClient) {
        std::cerr << "Failed to open JACK client. Is JACK server running?" << std::endl;
        std::cerr << "Try: jackd -d alsa -r 48000 -p 256" << std::endl;
        return 1;
    }


    jack_nframes_t jackSampleRate = jack_get_sample_rate(jackClient);
    jack_nframes_t jackBlockSize = jack_get_buffer_size(jackClient);

    // Create JACK output ports
    jack_port_t* outputPorts[settings.outputChannels];
    for (int ch = 0; ch < settings.outputChannels; ch++) {
        std::string portName = "output_" + std::to_string(ch + 1);
        outputPorts[ch] = jack_port_register(jackClient, portName.c_str(),
                                              JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!outputPorts[ch]) {
            std::cerr << "Failed to register JACK output port " << ch << std::endl;
            jack_client_close(jackClient);
            return 1;
        }
    }

    // Create JACK MIDI input port for control
    jack_port_t* midiInputPort = jack_port_register(jackClient, "midi_in",
                                                      JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    if (!midiInputPort) {
        std::cerr << "Warning: Failed to register JACK MIDI input port (MIDI control disabled)" << std::endl;
    }

    auto audioFilePlayer = std::make_unique<BufferedAudioFilePlayer>(settings.audioFilePath, jackSampleRate);

    if (!audioFilePlayer->isLoaded()) {
        std::cerr << "Error loading audio file: " << audioFilePlayer->getErrorMessage() << std::endl;
        return 1;
    }

    // Pre-fill buffer before starting audio callbacks
    audioFilePlayer->startPlayback();

    // Calculate file duration in output sample rate (for looping)
    double fileDuration = (double)audioFilePlayer->getTotalFrames() / audioFilePlayer->getFileSampleRate();
    uint64_t fileDurationFrames = (uint64_t)(fileDuration * jackSampleRate);

    std::cout << "Audio: " << settings.outputChannels << "ch @ " << jackSampleRate << " Hz ("
              << std::fixed << std::setprecision(1) << fileDuration << "s)" << std::endl;

    // Setup JACK callback context
    JackAudioContext jackContext;
    jackContext.audioPlayer = audioFilePlayer.get();
    jackContext.outputPorts = outputPorts;
    jackContext.numOutputChannels = settings.outputChannels;
    jackContext.client = jackClient;
    jackContext.fileDurationFrames = fileDurationFrames;
    jackContext.midiInputPort = midiInputPort;

    // Register JACK process callback
    if (jack_set_process_callback(jackClient, jackProcessCallback, &jackContext) != 0) {
        std::cerr << "Failed to set JACK process callback" << std::endl;
        jack_client_close(jackClient);
        return 1;
    }

    // Register as JACK Transport timebase master
    if (jack_set_timebase_callback(jackClient, 0, jackTimebaseCallback, &jackContext) != 0) {
        std::cerr << "Failed to set JACK timebase callback" << std::endl;
        jack_client_close(jackClient);
        return 1;
    }

    // Activate JACK client
    if (jack_activate(jackClient) != 0) {
        std::cerr << "Failed to activate JACK client" << std::endl;
        jack_client_close(jackClient);
        return 1;
    }

    // Auto-connect JACK ports to system playback
    const char** systemPorts = jack_get_ports(jackClient, "system:playback_", nullptr, JackPortIsInput);
    if (systemPorts) {
        for (int ch = 0; ch < settings.outputChannels && systemPorts[ch]; ch++) {
            std::string ourPort = "consoleAudioPlayer:output_" + std::to_string(ch + 1);
            jack_connect(jackClient, ourPort.c_str(), systemPorts[ch]);
        }
        jack_free(systemPorts);
    }

    // Auto-connect MIDI input to devices with "pico" or "CircuitPython" in name
    if (midiInputPort) {
        const char** midiPorts = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
        if (midiPorts) {
            bool connected = false;
            for (int i = 0; midiPorts[i] != nullptr; i++) {
                std::string portName = midiPorts[i];
                // Check if port name contains "pico" or "CircuitPython" (case-insensitive)
                std::string lowerName = portName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                if (lowerName.find("pico") != std::string::npos ||
                    lowerName.find("circuitpython") != std::string::npos) {
                    if (jack_connect(jackClient, midiPorts[i], jack_port_name(midiInputPort)) == 0) {
                        std::cout << "✓ MIDI: " << midiPorts[i] << std::endl;
                        connected = true;
                        break; // Connect to first matching device
                    }
                }
            }
            if (!connected) {
                std::cout << "⚠ No MIDI device found" << std::endl;
            }
            jack_free(midiPorts);
        }
    }

    std::cout << "Playing file: " << settings.audioFilePath << "..." << std::endl;

    // Start JACK Transport rolling
    jack_transport_start(jackClient);
    std::cout << "JACK Transport started" << std::endl;

    // Setup keyboard input
    auto termState = setupNonBlockingInput();

    std::cout << "\nKeyboard controls:" << std::endl;
    std::cout << "  SPACE - Pause/Resume" << std::endl;
    std::cout << "  S     - Stop and reset to beginning" << std::endl;
    std::cout << "  F     - Skip forward 10 seconds" << std::endl;
    std::cout << "  D     - Skip forward 30 seconds" << std::endl;
    std::cout << "  G     - Skip forward 60 seconds" << std::endl;
    std::cout << "  Q     - Quit" << std::endl << std::endl;

    bool running = true;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Check for keyboard input
        char key = getKeyPress();
        if (key != 0) {
            switch (key) {
                case ' ': // Space - toggle pause/play
                    if (audioFilePlayer->isStillPlaying()) {
                        audioFilePlayer->pause();
                        jack_transport_stop(jackClient);
                    } else {
                        jackContext.requestStop.store(false, std::memory_order_release);  // Clear stop lock
                        audioFilePlayer->play();
                        jack_transport_start(jackClient);
                    }
                    break;

                case 's':
                case 'S':
                    audioFilePlayer->stop();
                    jackContext.lastKnownPosition.store(0, std::memory_order_release);
                    jackContext.requestStop.store(true, std::memory_order_release);  // Lock at 0
                    jack_transport_locate(jackClient, 0);
                    jack_transport_stop(jackClient);
                    break;

                case 'f':
                case 'F': {
                    // Seek audio - timebase callback will update JACK automatically
                    audioFilePlayer->skipForward(10.0);
                    std::cout << "⏩ Skipped +10s" << std::endl;
                    break;
                }

                case 'd':
                case 'D': {
                    audioFilePlayer->skipForward(30.0);
                    std::cout << "⏩ Skipped +30s" << std::endl;
                    break;
                }

                case 'g':
                case 'G': {
                    audioFilePlayer->skipForward(60.0);
                    std::cout << "⏩ Skipped +60s" << std::endl;
                    break;
                }

                case 'q':
                case 'Q':
                    running = false;
                    break;
            }
        }

        // Check for loop detection from file reader
        if (audioFilePlayer->getLoopPlaybackDetected()) {
            std::cout << "↻  Loop detected - file wrapped to start" << std::endl;
            // Audio already looped seamlessly, JACK Transport will update automatically
        }

        // Handle MIDI transport requests (from audio callback)
        if (jackContext.requestPlay.exchange(false, std::memory_order_acquire)) {
            // If we're coming from stopped state, reset audio position first
            bool wasStoppedAtZero = jackContext.requestStop.exchange(false, std::memory_order_acq_rel);
            if (wasStoppedAtZero) {
                // Reset audio to beginning
                audioFilePlayer->stop();  // Resets fileReadPosition to 0 and clears buffer
                std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Let buffer refill from 0
                std::cout << "▶  Playing from start" << std::endl;
            }
            audioFilePlayer->play();
            jack_transport_start(jackClient);
        }
        if (jackContext.requestStop.load(std::memory_order_acquire)) {
            audioFilePlayer->stop();
            jackContext.lastKnownPosition.store(0, std::memory_order_release);  // Force position to 0

            // Immediately stop JACK Transport and reset to 0 (before delay, to prevent race)
            jack_transport_locate(jackClient, 0);
            jack_transport_stop(jackClient);

            // Brief delay to let buffer refill from position 0 (prevents playing stale data)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Keep position locked at 0
            jack_transport_locate(jackClient, 0);

            // DON'T clear requestStop flag - keep it set so timebase callback
            // continues forcing JACK position to EXACTLY 0 (prevents background
            // fill thread from updating position as it refills buffer).
            // Flag will be cleared when user presses play.
        }

        // Periodic MIDI auto-reconnect check (every 5 seconds)
        static int midiReconnectCount = 0;
        if (midiInputPort && midiReconnectCount++ % 5000 == 0) {
            // Check if MIDI port is connected
            const char** connections = jack_port_get_connections(midiInputPort);
            if (!connections) {
                // Not connected - try to find and connect to pico/CircuitPython
                const char** midiPorts = jack_get_ports(jackClient, nullptr, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
                if (midiPorts) {
                    for (int i = 0; midiPorts[i] != nullptr; i++) {
                        std::string portName = midiPorts[i];
                        std::string lowerName = portName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                        if (lowerName.find("pico") != std::string::npos ||
                            lowerName.find("circuitpython") != std::string::npos) {
                            if (jack_connect(jackClient, midiPorts[i], jack_port_name(midiInputPort)) == 0) {
                                break;
                            }
                        }
                    }
                    jack_free(midiPorts);
                }
            } else {
                jack_free(connections);
            }
        }

        // Monitor buffer health (disabled - enable if debugging buffer issues)
        // static int reportCount = 0;
        // if (reportCount++ % 10000 == 0) // Every 10 seconds
        // {
        //     uint32_t used = audioFilePlayer->getBufferUsedSlots();
        //     uint32_t total = audioFilePlayer->getBufferSize();
        //     double percentage = (double)used / total * 100.0;
        //     std::cout << "Buffer: " << used << "/" << total << " ("
        //              << std::fixed << std::setprecision(1) << percentage << "%)" << std::endl;
        // }
    }

    std::cout << "\nPlayback finished." << std::endl;

    // Restore terminal
    restoreTerminal(termState);

    jack_deactivate(jackClient);
    jack_client_close(jackClient);

    return 0;
}
