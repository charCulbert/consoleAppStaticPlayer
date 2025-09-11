#pragma once

#include "choc/audio/choc_SampleBuffers.h"
#include "AudioModule.h"
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>

#ifdef __APPLE__
    #include <AudioUnit/AudioUnit.h>
    #include <CoreAudio/CoreAudio.h>
    #include <AudioToolbox/AudioToolbox.h>
#elif __linux__
    #include <alsa/asoundlib.h>
    #include <pthread.h>
    #include <thread>
#endif

#define DEVICE_CHANNELS     2
#define DEVICE_SAMPLE_RATE  48000

class AudioEngine {
public:
    AudioEngine() : blockSize(512) {  // Start with very low latency
    }

    ~AudioEngine() {
        // Don't delete modules - they're owned by the caller
        releaseModules();
        modules.clear();
    }

    void addModule(AudioModule* module) {
        modules.push_back(module);
    }

    void removeModule(AudioModule* module) {
        auto it = std::remove(modules.begin(), modules.end(), module);
        if (it != modules.end())
            modules.erase(it, modules.end());
    }

    void prepareModules(int samplesPerBlock, double sampleRate) {
        for (auto* module : modules)
            module->prepareToPlay(samplesPerBlock, sampleRate);
    }

    void releaseModules() {
        for (auto* module : modules)
            module->releaseResources();
    }

#ifdef __APPLE__
    // Core Audio callback
    static OSStatus audioCallback(void* inRefCon,
                                AudioUnitRenderActionFlags* ioActionFlags,
                                const AudioTimeStamp* inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList* ioData) {
        
        auto* engine = static_cast<AudioEngine*>(inRefCon);
        
        // Create interleaved view for the output
        auto outputView = choc::buffer::createInterleavedView<float>(
            static_cast<float*>(ioData->mBuffers[0].mData),
            DEVICE_CHANNELS,
            inNumberFrames);
        
        // Clear the output buffer
        choc::buffer::setAllFrames(outputView, [](uint32_t){ return 0.0f; });
        
        // Render all modules
        for (auto* module : engine->modules)
            module->render(outputView);
        
        return noErr;
    }

    bool start() {
        // Get default output device
        AudioDeviceID deviceID;
        UInt32 size = sizeof(deviceID);
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        OSStatus result = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                   &propertyAddress,
                                                   0, nullptr,
                                                   &size, &deviceID);
        if (result != noErr) {
            std::cerr << "Failed to get default output device\n";
            return false;
        }
        
        // Set buffer size for ultra-low latency
        UInt32 bufferSize = blockSize;
        size = sizeof(bufferSize);
        propertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
        propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
        
        result = AudioObjectSetPropertyData(deviceID, &propertyAddress, 0, nullptr,
                                          size, &bufferSize);
        if (result != noErr) {
            std::cerr << "Warning: Could not set buffer size to " << blockSize << " frames\n";
        }
        
        // Check what buffer size actually got set
        UInt32 actualBufferSize;
        size = sizeof(actualBufferSize);
        result = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &size, &actualBufferSize);
        if (result == noErr) {
            std::cout << "Requested: " << blockSize << " frames, Actually got: " << actualBufferSize << " frames\n";
            // Now prepare modules with the ACTUAL buffer size
            prepareModules(actualBufferSize, DEVICE_SAMPLE_RATE);
        } else {
            std::cout << "Could not read actual buffer size\n";
            // Fallback to requested size if we can't read actual
            prepareModules(blockSize, DEVICE_SAMPLE_RATE);
        }
        
        // Create audio unit
        AudioComponentDescription desc = {};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        
        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) {
            std::cerr << "Failed to find audio component\n";
            return false;
        }
        
        result = AudioComponentInstanceNew(component, &audioUnit);
        if (result != noErr) {
            std::cerr << "Failed to create audio unit instance\n";
            return false;
        }
        
        // Set up stream format
        AudioStreamBasicDescription format = {};
        format.mSampleRate = DEVICE_SAMPLE_RATE;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        format.mChannelsPerFrame = DEVICE_CHANNELS;
        format.mFramesPerPacket = 1;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = sizeof(float) * DEVICE_CHANNELS;
        format.mBytesPerPacket = format.mBytesPerFrame;
        
        result = AudioUnitSetProperty(audioUnit,
                                    kAudioUnitProperty_StreamFormat,
                                    kAudioUnitScope_Input,
                                    0,
                                    &format,
                                    sizeof(format));
        if (result != noErr) {
            std::cerr << "Failed to set stream format\n";
            AudioComponentInstanceDispose(audioUnit);
            return false;
        }
        
        // Set callback
        AURenderCallbackStruct callbackStruct = {};
        callbackStruct.inputProc = audioCallback;
        callbackStruct.inputProcRefCon = this;
        
        result = AudioUnitSetProperty(audioUnit,
                                    kAudioUnitProperty_SetRenderCallback,
                                    kAudioUnitScope_Input,
                                    0,
                                    &callbackStruct,
                                    sizeof(callbackStruct));
        if (result != noErr) {
            std::cerr << "Failed to set render callback\n";
            AudioComponentInstanceDispose(audioUnit);
            return false;
        }
        
        // Initialize and start
        result = AudioUnitInitialize(audioUnit);
        if (result != noErr) {
            std::cerr << "Failed to initialize audio unit\n";
            AudioComponentInstanceDispose(audioUnit);
            return false;
        }
        
        result = AudioOutputUnitStart(audioUnit);
        if (result != noErr) {
            std::cerr << "Failed to start audio unit\n";
            AudioUnitUninitialize(audioUnit);
            AudioComponentInstanceDispose(audioUnit);
            return false;
        }

        return true;
    }
    
    void stop() {
        if (audioUnit) {
            AudioOutputUnitStop(audioUnit);
            // Give Core Audio time to finish any pending callbacks
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            AudioUnitUninitialize(audioUnit);
            AudioComponentInstanceDispose(audioUnit);
            audioUnit = nullptr;
        }
        releaseModules();
    }
    
private:
    AudioUnit audioUnit = nullptr;
    int blockSize;
    std::vector<AudioModule*> modules;
    
#elif __linux__
    // ALSA audio thread function
    void alsaAudioThread() {
        std::vector<float> buffer(blockSize * DEVICE_CHANNELS);
        
        while (alsaRunning) {
            // Create interleaved view for our buffer
            auto outputView = choc::buffer::createInterleavedView<float>(
                buffer.data(),
                DEVICE_CHANNELS,
                blockSize);
            
            // Clear buffer
            choc::buffer::setAllFrames(outputView, [](uint32_t){ return 0.0f; });
            
            // Render all modules
            for (auto* module : modules)
                module->render(outputView);
            
            // Write to ALSA
            int err = snd_pcm_writei(alsaHandle, buffer.data(), blockSize);
            if (err == -EAGAIN) {
                continue;
            } else if (err < 0) {
                if (snd_pcm_recover(alsaHandle, err, 0) < 0) {
                    std::cerr << "ALSA write error: " << snd_strerror(err) << std::endl;
                    break;
                }
            }
        }
    }


    bool start() {
        // Open ALSA device
        int err = snd_pcm_open(&alsaHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            std::cerr << "Cannot open audio device: " << snd_strerror(err) << std::endl;
            return false;
        }
        
        // Configure hardware parameters
        snd_pcm_hw_params_t* hwParams;
        snd_pcm_hw_params_alloca(&hwParams);
        
        err = snd_pcm_hw_params_any(alsaHandle, hwParams);
        if (err < 0) {
            std::cerr << "Cannot initialize hardware parameter structure: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set access type
        err = snd_pcm_hw_params_set_access(alsaHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            std::cerr << "Cannot set access type: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set sample format
        err = snd_pcm_hw_params_set_format(alsaHandle, hwParams, SND_PCM_FORMAT_FLOAT_LE);
        if (err < 0) {
            std::cerr << "Cannot set sample format: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set sample rate
        unsigned int rate = DEVICE_SAMPLE_RATE;
        err = snd_pcm_hw_params_set_rate_near(alsaHandle, hwParams, &rate, 0);
        if (err < 0) {
            std::cerr << "Cannot set sample rate: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set channel count
        err = snd_pcm_hw_params_set_channels(alsaHandle, hwParams, DEVICE_CHANNELS);
        if (err < 0) {
            std::cerr << "Cannot set channel count: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set buffer size
        snd_pcm_uframes_t requestedFrames = blockSize;
        err = snd_pcm_hw_params_set_buffer_size_near(alsaHandle, hwParams, &requestedFrames);
        if (err < 0) {
            std::cerr << "Cannot set buffer size: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Set period size (similar to Core Audio buffer size)
        snd_pcm_uframes_t requestedPeriod = blockSize;
        err = snd_pcm_hw_params_set_period_size_near(alsaHandle, hwParams, &requestedPeriod, 0);
        if (err < 0) {
            std::cerr << "Cannot set period size: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Apply hardware parameters
        err = snd_pcm_hw_params(alsaHandle, hwParams);
        if (err < 0) {
            std::cerr << "Cannot set parameters: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Get actual values
        snd_pcm_uframes_t actualBufferSize, actualPeriodSize;
        snd_pcm_hw_params_get_buffer_size(hwParams, &actualBufferSize);
        snd_pcm_hw_params_get_period_size(hwParams, &actualPeriodSize, 0);
        
        std::cout << "Requested: " << blockSize << " frames, Actually got: " << actualPeriodSize << " frames\n";
        std::cout << "Buffer size: " << actualBufferSize << " frames\n";
        
        // Update block size to actual period size
        blockSize = actualPeriodSize;
        
        // Prepare modules with actual buffer size
        prepareModules(blockSize, DEVICE_SAMPLE_RATE);
        
        // Prepare the audio interface
        err = snd_pcm_prepare(alsaHandle);
        if (err < 0) {
            std::cerr << "Cannot prepare audio interface: " << snd_strerror(err) << std::endl;
            snd_pcm_close(alsaHandle);
            return false;
        }
        
        // Start audio thread
        alsaRunning = true;
        alsaThread = std::thread(&AudioEngine::alsaAudioThread, this);
        
        std::cout << "ALSA engine started with " << blockSize << " frame buffer\n";
        return true;
    }
    
    void stop() {
        if (alsaRunning) {
            alsaRunning = false;
            if (alsaThread.joinable()) {
                alsaThread.join();
            }
        }
        if (alsaHandle) {
            snd_pcm_close(alsaHandle);
            alsaHandle = nullptr;
        }
        releaseModules();
    }
    
private:
    snd_pcm_t* alsaHandle = nullptr;
    std::thread alsaThread;
    bool alsaRunning = false;
    int blockSize;
    std::vector<AudioModule*> modules;

#else
    // Fallback for unsupported platforms
    bool start() {
        std::cerr << "Audio only supported on macOS and Linux\n";
        return false;
    }
    
    void stop() {}
    
private:
    int blockSize;
    std::vector<AudioModule*> modules;
#endif
};
