#pragma once

#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>

#include "AudioEngine.h"
#include "AudioFilePlayerModule.h"
#include "UdpSender.h"

class App
{
public:
    App() : running(true)
    {
        // Set up signal handler for graceful shutdown
        setupSignalHandler();
        
        UdpSender sender("127.0.0.1", 8080);
        // Send a few messages
        sender.send("Hello from the first message!");
        sender.send("This is the second message.");
        sender.send("And a final one before we exit.");

        std::string filePath = "../fanfare_test-8khz.wav";
        currentModule = new AudioFilePlayerModule(filePath);
        engine.addModule(currentModule);
        
        bool started = engine.start();
        if (!started) {
            std::cerr << "Failed to start audio engine\n";
            return;
        }
        
        std::cout << "Audio playing... Press Ctrl+C to exit\n";
        
        // Main loop - cleanup happens automatically in destructor
        runMainLoop();
    }
    
    ~App() {
        std::cout << "Audio stopped. Goodbye!\n";
        cleanup();
    }

private:
    AudioEngine engine;
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
    
    void cleanup() {
        // Prevent double cleanup
        if (cleanedUp.exchange(true)) {
            return;
        }
        
        std::cout << "Stopping audio engine...\n";
        // Stop audio engine first
        engine.stop();
        
        std::cout << "Removing module from engine...\n";
        // Remove module from engine before deleting
        if (currentModule) {
            engine.removeModule(currentModule);
        }
        
        std::cout << "Deleting module...\n";
        // Then clean up module
        if (currentModule) {
            delete currentModule;
            currentModule = nullptr;
        }
        
        std::cout << "Cleanup complete.\n";
        // Clear static instance
        instance = nullptr;
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
