#pragma once

#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <iostream>

//==============================================================================
/// Contains properties to control the choice and setup of audio devices when
/// creating an AudioEngine.
struct AudioDeviceInfo
{
    unsigned int id;                              ///< Device ID used to specify a device to the audio engine.
    std::string name;                             ///< Human-readable device name.
    unsigned int outputChannels;                  ///< Maximum output channels supported by device.
    bool isDefault;                               ///< true if this is the default output device.
    unsigned int preferredSampleRate;             ///< Preferred sample rate for this device.
    std::vector<unsigned int> supportedSampleRates; ///< List of supported sample rates.
};

//==============================================================================
/// Configuration options for audio engine initialization.
struct AudioEngineOptions
{
    /// Preferred sample rate, or 0 to use device default.
    double sampleRate = 48000;
    
    /// Preferred block size, or 0 to use device default.
    unsigned int blockSize = 512;
    
    /// Number of output channels required.
    unsigned int outputChannels = 2;
    
    /// Optional output device ID - 0 for default device.
    unsigned int outputDeviceId = 0;
    
    /// Optional flags for low-latency optimization.
    bool minimizeLatency = true;
};

//==============================================================================
/// A callback interface for receiving audio processing events from an AudioEngine.
class AudioEngineCallback
{
public:
    virtual ~AudioEngineCallback() = default;
    
    /// Called when the sample rate changes during operation.
    virtual void sampleRateChanged (double newRate) {}
    
    /// Called to render audio into the output buffer.
    /// @param outputBuffer The interleaved output buffer to fill with audio data.
    virtual void renderAudio (choc::buffer::InterleavedView<float> outputBuffer) = 0;
    
    /// Called when audio processing starts.
    virtual void audioStarted() {}
    
    /// Called when audio processing stops.
    virtual void audioStopped() {}
};

//==============================================================================
/// Abstract interface for audio engine implementations.
/// This allows different audio backends (RtAudio, CoreAudio, WASAPI, etc.) to be
/// used interchangeably with the same high-level AudioEngine API.
class AudioEngineInterface
{
public:
    virtual ~AudioEngineInterface() = default;
    
    /// Returns a list of available output devices on the system.
    virtual std::vector<AudioDeviceInfo> getAvailableOutputDevices() = 0;
    
    /// Returns information about the default output device.
    virtual AudioDeviceInfo getDefaultOutputDevice() = 0;
    
    /// Starts the audio engine with the specified options.
    /// @param options Configuration for sample rate, block size, device, etc.
    /// @returns true if the engine started successfully, false on error.
    virtual bool start (const AudioEngineOptions& options = {}) = 0;
    
    /// Stops the audio engine and releases all resources.
    virtual void stop() = 0;
    
    /// Returns true if the audio engine is currently running.
    virtual bool isRunning() const = 0;
    
    /// Returns the current audio block size in frames.
    virtual unsigned int getCurrentBlockSize() const = 0;
    
    /// Returns the current sample rate in Hz.
    virtual double getCurrentSampleRate() const = 0;
    
    /// Returns the last error message, or empty string if no error.
    virtual std::string getLastError() const = 0;
    
    /// Sets the callback object that will receive audio processing events.
    virtual void setCallback (AudioEngineCallback* callback) = 0;
    
    /// Returns the currently set callback object, or nullptr if none.
    virtual AudioEngineCallback* getCallback() const = 0;
    
protected:
    AudioEngineCallback* audioCallback = nullptr;
    std::string lastError;
    unsigned int currentBlockSize = 512;
    double currentSampleRate = 48000;
};


//==============================================================================
/// High-level audio engine that manages audio modules and delegates to a
/// pluggable audio backend implementation.
/// 
/// This class follows the composition pattern, allowing you to inject different
/// audio backend implementations (RtAudio, CoreAudio, etc.) while providing a
/// consistent high-level API for managing audio modules.
class AudioEngine : public AudioEngineCallback
{
public:
    /// Creates an AudioEngine using the specified backend implementation.
    /// @param engineImpl The audio backend to use (e.g. RtAudioEngine)
    AudioEngine (std::unique_ptr<AudioEngineInterface> engineImpl) 
        : audioEngineImpl (std::move (engineImpl))
    {
        audioEngineImpl->setCallback (this);
    }
    
    virtual ~AudioEngine()
    {
        stop();
        modules.clear();
    }
    
    //==============================================================================
    /// Adds an audio module to the processing chain.
    /// If audio is currently running, the module will be prepared immediately.
    void addModule (AudioModule* module)
    {
        modules.push_back (module);
        if (audioEngineImpl->isRunning())
        {
            module->prepareToPlay (audioEngineImpl->getCurrentBlockSize(), 
                                 audioEngineImpl->getCurrentSampleRate());
        }
    }
    
    /// Removes an audio module from the processing chain.
    void removeModule (AudioModule* module)
    {
        auto it = std::remove (modules.begin(), modules.end(), module);
        if (it != modules.end())
            modules.erase (it, modules.end());
    }
    
    //==============================================================================
    /// Engine control methods (delegated to backend implementation)
    
    std::vector<AudioDeviceInfo> getAvailableOutputDevices()
    {
        return audioEngineImpl->getAvailableOutputDevices();
    }
    
    AudioDeviceInfo getDefaultOutputDevice()
    {
        return audioEngineImpl->getDefaultOutputDevice();
    }
    
    bool start (const AudioEngineOptions& options = {})
    {
        return audioEngineImpl->start (options);
    }
    
    void stop()
    {
        audioEngineImpl->stop();
    }
    
    bool isRunning() const
    {
        return audioEngineImpl->isRunning();
    }
    
    unsigned int getCurrentBlockSize() const
    {
        return audioEngineImpl->getCurrentBlockSize();
    }
    
    double getCurrentSampleRate() const
    {
        return audioEngineImpl->getCurrentSampleRate();
    }
    
    std::string getLastError() const
    {
        return audioEngineImpl->getLastError();
    }
    
    //==============================================================================
    /// AudioEngineCallback implementation - handles audio processing
    
    void sampleRateChanged (double newRate) override
    {
        // Re-prepare all modules with new sample rate
        for (auto* module : modules)
            module->prepareToPlay (audioEngineImpl->getCurrentBlockSize(), newRate);
    }
    
    void renderAudio (choc::buffer::InterleavedView<float> outputBuffer) override
    {
        // Clear the output buffer
        choc::buffer::setAllFrames (outputBuffer, [](uint32_t){ return 0.0f; });
        
        // Render all modules additively
        for (auto* module : modules)
            module->render (outputBuffer);
    }
    
    void audioStarted() override
    {
        // Prepare all modules when audio starts
        for (auto* module : modules)
        {
            module->prepareToPlay (audioEngineImpl->getCurrentBlockSize(), 
                                 audioEngineImpl->getCurrentSampleRate());
        }
    }
    
    void audioStopped() override
    {
        // Release all module resources when audio stops
        for (auto* module : modules)
            module->releaseResources();
    }

private:
    std::unique_ptr<AudioEngineInterface> audioEngineImpl;
    std::vector<AudioModule*> modules;
};