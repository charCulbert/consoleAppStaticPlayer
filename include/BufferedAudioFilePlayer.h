#pragma once

#include "choc/audio/choc_AudioFileFormat.h"
#include "choc/audio/choc_AudioSampleData.h"
#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"
#include "choc/threading/choc_TaskThread.h"
#include <string>
#include <memory>
#include <atomic>

// Simple buffered audio file player using CHOC's FIFO for low-memory systems
// Keeps a small ring buffer (few seconds) filled by background thread
class BufferedAudioFilePlayer
{
public:
    BufferedAudioFilePlayer(const std::string& filePath, double outputSampleRate = 48000.0);
    ~BufferedAudioFilePlayer();

    void setOutputSampleRate(double rate);

    // Audio processing (called from JACK callback)
    void processBlock(choc::buffer::ChannelArrayView<float> output);

    bool isLoaded() const { return fileLoaded; }
    bool isStillPlaying() const { return isPlaying; }
    std::string getErrorMessage() const { return errorMessage; }

    void startPlayback(); // Call this before adding to audio player

    // Playback control
    void play() { isPlaying = true; }
    void pause() { isPlaying = false; }
    void stop() { isPlaying = false; fileReadPosition = 0; totalSamplesPlayed = 0; audioBuffer.reset(bufferSize); }
    uint64_t skipForward(double seconds);  // Returns new position (for JACK sync)

    // Volume control (0.0 to 1.0)
    void setGain(float gain) { currentGain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed); }
    float getGain() const { return currentGain.load(std::memory_order_relaxed); }

    // For monitoring buffer health
    uint32_t getBufferUsedSlots() const { return audioBuffer.getUsedSlots(); }
    uint32_t getBufferSize() const { return bufferSize; }

    // For loop detection
    std::atomic<bool> getLoopPlaybackDetected() { return loopPlaybackDetected.exchange(false); }

    // Get current playback position in output sample rate (for JACK Transport)
    uint64_t getCurrentOutputFrame() const {
        // Return actual playback position (samples sent to speakers)
        return totalSamplesPlayed.load(std::memory_order_relaxed);
    }

    // File info (for JACK Transport sync)
    uint64_t getTotalFrames() const { return totalFrames; }
    double getFileSampleRate() const { return fileSampleRate; }
    double getOutputSampleRate() const { return outputSampleRate; }

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
    std::atomic<float> currentGain{1.0f};  // Volume: 0.0 = silence, 1.0 = full
    std::string errorMessage;

    // Simple interleaved FIFO buffer
    choc::fifo::SingleReaderSingleWriterFIFO<float> audioBuffer;
    static constexpr uint32_t bufferSizeSeconds = 3; // Keep 3 seconds buffered
    uint32_t bufferSize = 0; // Will be calculated based on sample rate

    // File reading state
    std::atomic<uint64_t> fileReadPosition{0};

    // Playback position tracking (actual samples sent to output)
    std::atomic<uint64_t> totalSamplesPlayed{0};

    // Background loading
    choc::threading::TaskThread backgroundThread;
    std::atomic<bool> shouldStopLoading{false};

    // Loop detection
    std::atomic<bool> loopPlaybackDetected{false};

    bool loadAudioFile();
    void backgroundLoadingTask();
    void fillBufferFromFile();
    uint32_t getBufferSizeForSampleRate(double sampleRate) const;
};