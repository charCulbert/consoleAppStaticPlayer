#pragma once

#include "choc/audio/io/choc_AudioMIDIPlayer.h"
#include "choc/audio/choc_AudioFileFormat.h"
#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "choc/threading/choc_TaskThread.h"
#include <string>
#include <memory>
#include <atomic>

// Simple buffered audio file player using CHOC's FIFO for low-memory systems
// Keeps a small ring buffer (few seconds) filled by background thread
class BufferedAudioFilePlayer : public choc::audio::io::AudioMIDICallback
{
public:
    BufferedAudioFilePlayer(const std::string& filePath, double outputSampleRate = 48000.0);
    ~BufferedAudioFilePlayer() override;

    void sampleRateChanged(double newRate) override;
    void setOutputSampleRate(double rate);
    void startBlock() override {}
    void processSubBlock(const choc::audio::AudioMIDIBlockDispatcher::Block& block,
                         bool replaceOutput) override;
    void endBlock() override {}

    bool isLoaded() const { return fileLoaded; }
    bool isStillPlaying() const { return isPlaying; }
    std::string getErrorMessage() const { return errorMessage; }

    void startPlayback(); // Call this before adding to audio player

    // For monitoring buffer health
    uint32_t getBufferUsedSlots() const { return audioBuffer.getUsedSlots(); }
    uint32_t getBufferSize() const { return bufferSize; }

private:
    std::string filePath;
    std::shared_ptr<std::ifstream> fileStream;
    std::unique_ptr<choc::audio::AudioFileReader> fileReader;

    double fileSampleRate = 0.0;
    double outputSampleRate = 48000.0;
    uint32_t numChannels = 0;
    uint64_t totalFrames = 0;

    std::atomic<bool> isPlaying{true};
    std::atomic<bool> fileLoaded{false};
    std::string errorMessage;

    // Simple interleaved FIFO buffer
    choc::fifo::SingleReaderSingleWriterFIFO<float> audioBuffer;
    static constexpr uint32_t bufferSizeSeconds = 3; // Keep 3 seconds buffered
    uint32_t bufferSize = 0; // Will be calculated based on sample rate

    // File reading state
    std::atomic<uint64_t> fileReadPosition{0};

    // Background loading
    choc::threading::TaskThread backgroundThread;
    std::atomic<bool> shouldStopLoading{false};

    bool loadAudioFile();
    void backgroundLoadingTask();
    void fillBufferFromFile();
    uint32_t getBufferSizeForSampleRate(double sampleRate) const;
};