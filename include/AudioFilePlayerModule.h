#pragma once
#include "AudioModule.h"
#include "choc/audio/choc_AudioFileFormat.h"
#include <string>
#include <fstream>
#include <memory>

class AudioFilePlayerModule : public AudioModule {
public:
    // Constructor takes the path to the WAV file.
    AudioFilePlayerModule(const std::string& filePath);
    ~AudioFilePlayerModule() override = default;

    // Standard lifecycle methods.
    void prepareToPlay(int samplesPerBlock, double sampleRate) override;
    void releaseResources() override;
    void render(choc::buffer::InterleavedView<float> outputBuffer) override;
    
    /// Returns the native sample rate of the loaded audio file
    double getNativeSampleRate() const override { return nativeSampleRate; }
    
    /// Checks if resampling is needed and resamples if required
    void resampleIfNeeded(double targetSampleRate);

private:
    std::string filePath;
    choc::audio::AudioFileData audioData; // Loaded audio data.
    uint32_t readPosition = 0;
    double currentSampleRate = 0.0;
    double nativeSampleRate = 0.0;  // Store the file's original sample rate
};
