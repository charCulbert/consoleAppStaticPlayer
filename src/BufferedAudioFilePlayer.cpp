#include "../include/BufferedAudioFilePlayer.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iomanip>

BufferedAudioFilePlayer::BufferedAudioFilePlayer(const std::string& filePath, double outputSampleRate)
    : filePath(filePath), outputSampleRate(outputSampleRate)
{
    if (!loadAudioFile())
    {
        isPlaying = false;
        return;
    }

    // Calculate buffer size for output sample rate (interleaved samples)
    bufferSize = getBufferSizeForSampleRate(outputSampleRate) * numChannels;
    audioBuffer.reset(bufferSize);

    std::cout << "BufferedAudioFilePlayer initialized:" << std::endl;
    std::cout << "  File: " << filePath << std::endl;
    std::cout << "  File sample rate: " << fileSampleRate << " Hz" << std::endl;
    std::cout << "  Output sample rate: " << outputSampleRate << " Hz" << std::endl;
    std::cout << "  Channels: " << numChannels << std::endl;
    std::cout << "  Total frames: " << totalFrames << std::endl;
    std::cout << "  Buffer size: " << bufferSize << " samples (" << (bufferSize / numChannels) << " frames)" << std::endl;

    bool needsResampling = (std::abs(fileSampleRate - outputSampleRate) > 0.1);
    if (needsResampling)
    {
        double ratio = fileSampleRate / outputSampleRate;
        std::cout << "  Resampling: " << fileSampleRate << " Hz -> " << outputSampleRate
                  << " Hz (ratio: " << std::fixed << std::setprecision(3) << ratio << ")" << std::endl;
    }
    else
    {
        std::cout << "  No resampling needed (rates match)" << std::endl;
    }

    // Don't start audio output yet - wait for explicit startPlayback() call
    isPlaying = false;
}

BufferedAudioFilePlayer::~BufferedAudioFilePlayer()
{
    shouldStopLoading = true;
    backgroundThread.stop();
}

bool BufferedAudioFilePlayer::loadAudioFile()
{
    try
    {
        fileStream = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!fileStream || !fileStream->is_open())
        {
            errorMessage = "Could not open file: " + filePath;
            return false;
        }

        choc::audio::AudioFileFormatList formatList;
        formatList.addFormat<choc::audio::WAVAudioFileFormat<false>>();

        fileReader = formatList.createReader(fileStream);
        if (!fileReader)
        {
            errorMessage = "Unsupported audio file format";
            return false;
        }

        auto properties = fileReader->getProperties();
        fileSampleRate = properties.sampleRate;
        numChannels = properties.numChannels;
        totalFrames = properties.numFrames;

        if (numChannels == 0)
        {
            errorMessage = "Invalid audio file format";
            return false;
        }

        fileLoaded = true;
        std::cout << "Audio file loaded successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = "Error loading audio file: " + std::string(e.what());
        return false;
    }
}

uint32_t BufferedAudioFilePlayer::getBufferSizeForSampleRate(double sampleRate) const
{
    return static_cast<uint32_t>(bufferSizeSeconds * sampleRate);
}

void BufferedAudioFilePlayer::setOutputSampleRate(double rate)
{
    outputSampleRate = rate;
    // Recalculate buffer size for the new rate
    bufferSize = getBufferSizeForSampleRate(outputSampleRate) * numChannels;
    audioBuffer.reset(bufferSize);
}

void BufferedAudioFilePlayer::skipForward(double seconds)
{
    // Calculate new file position
    uint64_t samplesToSkip = (uint64_t)(seconds * fileSampleRate);
    uint64_t newPosition = fileReadPosition.load() + samplesToSkip;

    // Wrap around if past end
    if (newPosition >= totalFrames) {
        newPosition = newPosition % totalFrames;
    }

    // Update positions
    fileReadPosition = newPosition;
    totalSamplesPlayed = newPosition;
    currentAudioPosition = (double)newPosition / fileSampleRate;

    // Clear buffer (old data is stale after seek)
    audioBuffer.reset(bufferSize);
}

void BufferedAudioFilePlayer::startPlayback()
{
    if (!fileLoaded) return;

    // Pre-fill buffer for clean startup
    std::cout << "Pre-filling buffer..." << std::endl;

    // Fill buffer more aggressively at startup
    uint32_t targetFill = bufferSize * 9/10; // Fill to 90% before starting
    while (audioBuffer.getUsedSlots() < targetFill)
    {
        fillBufferFromFile();

        // If we can't fill more, break to avoid infinite loop
        static uint32_t lastFillLevel = 0;
        uint32_t currentFill = audioBuffer.getUsedSlots();
        if (currentFill == lastFillLevel)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            static int stuckCount = 0;
            if (++stuckCount > 10) break; // Give up after 50ms of no progress
        }
        else
        {
            lastFillLevel = currentFill;
        }
    }

    double fillPercentage = (double)audioBuffer.getUsedSlots() / bufferSize * 100.0;
    std::cout << "Initial buffer fill: " << audioBuffer.getUsedSlots()
              << " samples (" << std::fixed << std::setprecision(1) << fillPercentage << "%)" << std::endl;

    // Start background loading thread
    backgroundThread.start(10, [this] { backgroundLoadingTask(); });

    // Now ready to play - enable audio output
    isPlaying = true;
    std::cout << "Ready for audio playback!" << std::endl;
}

void BufferedAudioFilePlayer::backgroundLoadingTask()
{
    if (shouldStopLoading || !fileLoaded)
        return;

    // Keep buffer filled
    if (audioBuffer.getFreeSlots() > numChannels * 512) // If we have space for 512+ frames
    {
        fillBufferFromFile();
    }
}

void BufferedAudioFilePlayer::fillBufferFromFile()
{
    static constexpr uint32_t chunkFrames = 1024; // Read 1024 frames at a time

    uint32_t freeSlots = audioBuffer.getFreeSlots();
    uint32_t freeFrames = freeSlots / numChannels;

    if (freeFrames < chunkFrames)
        return; // Not enough space

    uint32_t framesToRead = std::min(chunkFrames, freeFrames);
    uint64_t currentFilePos = fileReadPosition.load();

    // Handle file looping
    if (currentFilePos >= totalFrames)
    {
        currentFilePos = 0;
        fileReadPosition = 0;
        loopPlaybackDetected.store(true, std::memory_order_release);
    }

    try
    {
        // Calculate actual frames to read (don't read past end of file)
        uint32_t availableFrames = static_cast<uint32_t>(totalFrames - currentFilePos);
        uint32_t actualFramesToRead = std::min(framesToRead, availableFrames);

        if (actualFramesToRead == 0)
        {
            // End of file, loop back
            currentFilePos = 0;
            fileReadPosition = 0;
            loopPlaybackDetected.store(true, std::memory_order_release);
            return;
        }

        // Check if we need resampling
        bool needsResampling = (std::abs(fileSampleRate - outputSampleRate) > 0.1);

        if (needsResampling)
        {
            // Use simple linear interpolation for real-time performance
            double sampleRateRatio = fileSampleRate / outputSampleRate;
            uint32_t fileFramesToRead = static_cast<uint32_t>(actualFramesToRead * sampleRateRatio) + 2;
            fileFramesToRead = std::min(fileFramesToRead, availableFrames);

            // Create buffer for file data
            choc::buffer::ChannelArrayBuffer<float> fileBuffer(
                choc::buffer::Size::create(numChannels, fileFramesToRead));

            // Read from file
            auto fileView = fileBuffer.getView().getStart(fileFramesToRead);
            bool success = fileReader->readFrames(currentFilePos, fileView);

            if (success)
            {
                // Cubic interpolation - better quality than linear, still efficient
                for (uint32_t outFrame = 0; outFrame < actualFramesToRead; ++outFrame)
                {
                    double sourcePos = outFrame * sampleRateRatio;
                    uint32_t sourceFrame = static_cast<uint32_t>(sourcePos);
                    double fraction = sourcePos - sourceFrame;

                    if (sourceFrame + 3 < fileFramesToRead && sourceFrame > 0)
                    {
                        // Cubic interpolation using 4 points
                        for (uint32_t channel = 0; channel < numChannels; ++channel)
                        {
                            float y0 = fileView.getSample(channel, sourceFrame - 1);
                            float y1 = fileView.getSample(channel, sourceFrame);
                            float y2 = fileView.getSample(channel, sourceFrame + 1);
                            float y3 = fileView.getSample(channel, sourceFrame + 2);

                            // Catmull-Rom cubic interpolation
                            float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
                            float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                            float a2 = -0.5f * y0 + 0.5f * y2;
                            float a3 = y1;

                            float t = static_cast<float>(fraction);
                            float interpolated = a0 * t * t * t + a1 * t * t + a2 * t + a3;

                            if (!audioBuffer.push(interpolated))
                            {
                                fileReadPosition = currentFilePos + sourceFrame;
                                return;
                            }
                        }
                    }
                    else if (sourceFrame + 1 < fileFramesToRead)
                    {
                        // Fall back to linear interpolation at boundaries
                        for (uint32_t channel = 0; channel < numChannels; ++channel)
                        {
                            float sample1 = fileView.getSample(channel, sourceFrame);
                            float sample2 = fileView.getSample(channel, sourceFrame + 1);
                            float interpolated = sample1 + fraction * (sample2 - sample1);

                            if (!audioBuffer.push(interpolated))
                            {
                                fileReadPosition = currentFilePos + sourceFrame;
                                return;
                            }
                        }
                    }
                    else if (sourceFrame < fileFramesToRead)
                    {
                        // At end, just use the last sample
                        for (uint32_t channel = 0; channel < numChannels; ++channel)
                        {
                            float sample = fileView.getSample(channel, sourceFrame);
                            if (!audioBuffer.push(sample))
                            {
                                fileReadPosition = currentFilePos + sourceFrame;
                                return;
                            }
                        }
                    }
                }

                // Update file position
                fileReadPosition = currentFilePos + fileFramesToRead;
            }
            else
            {
                std::cerr << "Failed to read from audio file during resampling" << std::endl;
            }
        }
        else
        {
            // No resampling needed - direct copy
            choc::buffer::ChannelArrayBuffer<float> readBuffer(
                choc::buffer::Size::create(numChannels, actualFramesToRead));

            // Read frames from file
            auto readView = readBuffer.getView().getStart(actualFramesToRead);
            bool success = fileReader->readFrames(currentFilePos, readView);

            if (success)
            {
                // Push samples to FIFO buffer in interleaved format
                for (uint32_t frame = 0; frame < actualFramesToRead; ++frame)
                {
                    for (uint32_t channel = 0; channel < numChannels; ++channel)
                    {
                        float sample = readView.getSample(channel, frame);

                        // If buffer is full, we're done for now
                        if (!audioBuffer.push(sample))
                        {
                            fileReadPosition = currentFilePos + frame;
                            return;
                        }
                    }
                }

                // Update file position
                fileReadPosition = currentFilePos + actualFramesToRead;
            }
            else
            {
                std::cerr << "Failed to read from audio file" << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading from audio file: " << e.what() << std::endl;
    }
}

void BufferedAudioFilePlayer::processBlock(choc::buffer::ChannelArrayView<float> output)
{
    // Always clear output first to avoid clicks/pops
    output.clear();

    if (!isPlaying || !fileLoaded)
    {
        return;
    }

    auto numFrames = output.getNumFrames();
    auto numOutputChannels = output.getNumChannels();

    // Check if we have enough samples in buffer
    uint32_t samplesNeeded = numFrames * numChannels;
    if (audioBuffer.getUsedSlots() < samplesNeeded)
    {
        // Buffer underrun - output silence (already cleared)
        // NOTE: Avoid I/O in audio callback - uncomment for debugging only
        // static int underrunCount = 0;
        // if (underrunCount++ % 100 == 0)
        // {
        //     std::cout << "Buffer underrun! Need " << samplesNeeded << " samples, have "
        //              << audioBuffer.getUsedSlots() << std::endl;
        // }
        return;
    }

    // Read samples from buffer and convert from interleaved to channel format
    float gain = currentGain.load(std::memory_order_relaxed);

    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        // Read one frame of interleaved samples
        float frameSamples[8] = {0}; // Support up to 8 channels

        for (uint32_t channel = 0; channel < numChannels && channel < 8; ++channel)
        {
            audioBuffer.pop(frameSamples[channel]);
        }

        // Copy to output channels with gain applied
        for (uint32_t channel = 0; channel < numOutputChannels; ++channel)
        {
            uint32_t sourceChannel = std::min(channel, numChannels - 1);
            float sample = frameSamples[sourceChannel] * gain;
            output.getSample(channel, frame) = sample;
        }
    }

    // Position is tracked via fileReadPosition - no additional counter needed
}

uint64_t BufferedAudioFilePlayer::skipForward(double seconds)
{
    if (!fileLoaded) return getCurrentOutputFrame();

    // Calculate new file position (in file's sample rate)
    uint64_t framesToSkip = static_cast<uint64_t>(seconds * fileSampleRate);
    uint64_t currentFilePos = fileReadPosition.load();
    uint64_t newFilePos = currentFilePos + framesToSkip;

    // Handle wrap-around if we skip past the end
    if (newFilePos >= totalFrames)
    {
        newFilePos = newFilePos % totalFrames;
    }

    // Just update file position atomically - buffer will refill automatically
    fileReadPosition.store(newFilePos, std::memory_order_release);

    // Clear buffer so we don't play stale audio
    audioBuffer.reset(bufferSize);

    std::cout << "Seek to " << std::fixed << std::setprecision(2)
              << (double)newFilePos / fileSampleRate << "s" << std::endl;

    return getCurrentOutputFrame();
}