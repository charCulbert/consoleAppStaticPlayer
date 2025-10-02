#include "../include/BufferedAudioFilePlayer.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iomanip>

using std::cerr;

BufferedAudioFilePlayer::BufferedAudioFilePlayer(const std::string &filePath,
                                                 double outputSampleRate)
    : filePath(filePath), outputSampleRate(outputSampleRate) {
  if (!loadAudioFile()) {
    return;
  }

  // Calculate buffer size for output sample rate (interleaved samples)
  bufferSize = getBufferSizeForSampleRate(outputSampleRate) * numChannels;
  audioBuffer.reset(bufferSize);

  std::cout << "File: " << filePath << std::endl;
  std::cout << "  Sample rate: " << fileSampleRate << " Hz" << std::endl;
  std::cout << "  Channels: " << numChannels << std::endl;
  std::cout << "  Frames: " << totalFrames << std::endl;
  std::cout << "  Duration: " << std::fixed << std::setprecision(1)
            << (double)totalFrames / fileSampleRate << "s" << std::endl;

  bool needsResampling = (std::abs(fileSampleRate - outputSampleRate) > 0.1);
  if (needsResampling) {
    double ratio = fileSampleRate / outputSampleRate;
    std::cout << "  Resampling: " << std::fixed << std::setprecision(3)
              << ratio << "x" << std::endl;
  }
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

void BufferedAudioFilePlayer::initializeBuffer()
{
    if (!fileLoaded) return;

    std::cout << "Pre-buffering..." << std::endl;

    // Pre-fill buffer
    uint32_t targetFill = bufferSize * 9/10; // Fill to 90%
    while (audioBuffer.getUsedSlots() < targetFill)
    {
        fillBufferFromFile();

        // Avoid infinite loop if we can't fill more
        static uint32_t lastFillLevel = 0;
        uint32_t currentFill = audioBuffer.getUsedSlots();
        if (currentFill == lastFillLevel)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            static int stuckCount = 0;
            if (++stuckCount > 10) break;
        }
        else
        {
            lastFillLevel = currentFill;
        }
    }

    // Start background loading thread
    backgroundThread.start(10, [this] { backgroundLoadingTask(); });
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
              cerr << "Failed to read from audio file during resampling"
                   << std::endl;
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
              cerr << "Failed to read from audio file" << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
      cerr << "Error reading from audio file: " << e.what() << std::endl;
    }
}

void BufferedAudioFilePlayer::processBlock(choc::buffer::ChannelArrayView<float> output)
{
    output.clear();

    if (!fileLoaded) return;

    auto numFrames = output.getNumFrames();
    auto numOutputChannels = output.getNumChannels();

    // Check if we have enough samples in buffer
    uint32_t samplesNeeded = numFrames * numChannels;
    if (audioBuffer.getUsedSlots() < samplesNeeded) {
        // Buffer underrun - output silence (already cleared)
        return;
    }

    // Read samples from buffer (interleaved) and write to output (channel array)
    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        float frameSamples[8] = {0}; // Support up to 8 channels

        for (uint32_t channel = 0; channel < numChannels && channel < 8; ++channel)
        {
            audioBuffer.pop(frameSamples[channel]);
        }

        // Copy to output channels
        for (uint32_t channel = 0; channel < numOutputChannels; ++channel)
        {
            uint32_t sourceChannel = std::min(channel, numChannels - 1);
            output.getSample(channel, frame) = frameSamples[sourceChannel];
        }
    }
}
