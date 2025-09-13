#pragma once

#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <memory>

#include "RtAudioEngine.h"
#include "AudioEngine.h"
#include "AudioFilePlayerModule.h"
#include "UdpSender.h"

/// Main application class that manages audio playback and system lifecycle.
/// Demonstrates the use of the modular AudioEngine architecture with RtAudio backend.
class App
{
public:
    App() : running (true)
    {
        setupSignalHandler();
        
        // Send some test UDP messages
        UdpSender sender ("127.0.0.1", 8080);
        sender.send ("Hello from the first message!");
        sender.send ("Hello from the second message!");

        // Create audio engine with RtAudio backend
        auto rtAudioImpl = std::make_unique<RtAudioEngine>();
        audioEngine = std::make_unique<AudioEngine> (std::move (rtAudioImpl));
        
        // Create audio module and get sample rate IMMEDIATELY
        std::string filePath = "../test_6ch_48k.wav";

        // get SR INFO about the file...
        float fileSampleRate = 0; // dummy thing
        // create the module and add it to the engine
        currentModule = new AudioFilePlayerModule (filePath);
        audioEngine->addModule (currentModule);

        // Smart sample rate negotiation
        auto defaultDevice = audioEngine->getDefaultOutputDevice();

        // Check if the device supports the file's native sample rate
        bool deviceSupportsFileRate = false;
        for (auto rate : defaultDevice.supportedSampleRates)
        {
            if (std::abs(rate - fileSampleRate) < 0.1)
            {
                deviceSupportsFileRate = true;
                break;
            }
        }

        // Configure audio settings
        AudioEngineOptions options;
        if (deviceSupportsFileRate)
        {
            // Use file's native rate - no resampling needed!
            options.sampleRate = fileSampleRate;
            std::cout << "Using file's native sample rate: " << fileSampleRate << " Hz" << std::endl;
        }
        else
        {
            // Use device's preferred rate - choc will resample
            options.sampleRate = defaultDevice.preferredSampleRate;
            std::cout << "File rate " << fileSampleRate << " Hz not supported, using device preferred: "
                      << defaultDevice.preferredSampleRate << " Hz" << std::endl;
        }

        options.blockSize = 512;
        options.outputChannels = 2;
        options.minimizeLatency = true;

        // Start audio playback
        bool started = audioEngine->start (options);
        if (! started) {
            std::cerr << "Failed to start audio engine: " << audioEngine->getLastError() << std::endl;
            return;
        }

        std::cout << "Audio playing on: " << audioEngine->getDefaultOutputDevice().name << std::endl;
        std::cout << "Sample rate: " << audioEngine->getCurrentSampleRate() << " Hz" << std::endl;
        std::cout << "Block size: " << audioEngine->getCurrentBlockSize() << " frames" << std::endl;
        std::cout << "Press Ctrl+C to exit..." << std::endl;

        runMainLoop();
    }
    
    ~App() {
        std::cout << "Shutting down gracefully..." << std::endl;
    }

private:
    std::unique_ptr<AudioEngine> audioEngine;
    AudioModule* currentModule = nullptr;
    std::atomic<bool> running;
    std::atomic<bool> cleanedUp{false};
    
    static App* instance;
    
    void setupSignalHandler() {
        instance = this;
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
    }
    
    static void signalHandler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down gracefully...\n";
        if (instance) {
            instance->running = false;
        }
    }
    void runMainLoop() {
        // Simple polling loop - works everywhere
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

// Static member definition
App* App::instance = nullptr;
