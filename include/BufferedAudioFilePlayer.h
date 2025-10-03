#pragma once

#include "choc/audio/choc_AudioFileFormat.h"
#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "choc/threading/choc_TaskThread.h"
#include <string>
#include <memory>
#include <atomic>

// Simple buffered audio file player using CHOC's FIFO
// Keeps a small ring buffer (few seconds) filled by background thread
class BufferedAudioFilePlayer
{
public:
    BufferedAudioFilePlayer(const std::string& filePath, double outputSampleRate);
    ~BufferedAudioFilePlayer();

    // Audio processing (called from JACK callback)
    void processBlock(choc::buffer::ChannelArrayView<float> output);

    bool isLoaded() const { return fileLoaded; }
    std::string getErrorMessage() const { return errorMessage; }

    uint64_t getTotalFrames() const { return totalFrames; }
    uint32_t getNumChannels() const { return numChannels; }
    double getFileSampleRate() const { return fileSampleRate; }

private:
    std::string filePath;
    std::string errorMessage;
    std::shared_ptr<std::ifstream> fileStream;
    std::unique_ptr<choc::audio::AudioFileReader> fileReader;

    double fileSampleRate = 0.0;
    double outputSampleRate = 48000.0;
    uint32_t numChannels = 0;
    uint64_t totalFrames = 0;

    std::atomic<bool> fileLoaded{false};

    // Full audio file loaded into memory (channel-interleaved)
    std::vector<float> audioData;
    std::atomic<uint64_t> playbackPosition{0}; // Current playback position in frames

    bool loadAudioFile();
};
