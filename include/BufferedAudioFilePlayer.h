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

    // Playback control
    void play() { isPlaying = true; }
    void pause() { isPlaying = false; }
    void stop() { isPlaying = false; fileReadPosition = 0; audioBuffer.reset(bufferSize); totalSamplesPlayed = 0; currentAudioPosition = 0.0; }

    // For monitoring buffer health
    uint32_t getBufferUsedSlots() const { return audioBuffer.getUsedSlots(); }
    uint32_t getBufferSize() const { return bufferSize; }

    // For loop detection and UDP messaging
    std::atomic<bool> getLoopPlaybackDetected() { return loopPlaybackDetected.exchange(false); }

    // Audio clock sync - get current playback position (lock-free, safe from any thread)
    double getCurrentAudioPosition() const { return currentAudioPosition.load(std::memory_order_relaxed); }
    uint64_t getTotalSamplesPlayed() const { return totalSamplesPlayed.load(std::memory_order_relaxed); }

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

    // Loop detection atomics
    std::atomic<uint32_t> loopSequenceNumber{0};
    std::atomic<uint32_t> samplesUntilLoopAudible{0};
    std::atomic<bool> loopPlaybackDetected{false};

    // Audio clock synchronization - lock-free for real-time thread safety
    std::atomic<double> currentAudioPosition{0.0};
    std::atomic<uint64_t> totalSamplesPlayed{0};

    bool loadAudioFile();
    void backgroundLoadingTask();
    void fillBufferFromFile();
    uint32_t getBufferSizeForSampleRate(double sampleRate) const;
};