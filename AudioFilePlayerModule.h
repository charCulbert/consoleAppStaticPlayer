#pragma once
#include "AudioModule.h"
#include "choc/audio/choc_AudioFileFormat.h" // Adjust as needed
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

private:
    std::string filePath;
    choc::audio::AudioFileData audioData; // Loaded audio data.
    uint32_t readPosition = 0;
    double currentSampleRate = 48000.0;
};
