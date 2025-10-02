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

    void initializeBuffer();

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

    // Simple interleaved FIFO buffer
    choc::fifo::SingleReaderSingleWriterFIFO<float> audioBuffer;
    static constexpr uint32_t bufferSizeSeconds = 3; // Keep 3 seconds buffered
    uint32_t bufferSize = 0; // Will be calculated based on sample rate

    // File reading state (for background thread to know where to read from)
    std::atomic<uint64_t> fileReadPosition{0};

    // Background loading
    choc::threading::TaskThread backgroundThread;
    std::atomic<bool> shouldStopLoading{false};

    bool loadAudioFile();
    void backgroundLoadingTask();
    void fillBufferFromFile();
    uint32_t getBufferSizeForSampleRate(double sampleRate) const;
};
