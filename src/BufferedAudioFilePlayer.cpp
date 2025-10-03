#include "../include/BufferedAudioFilePlayer.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iomanip>


BufferedAudioFilePlayer::BufferedAudioFilePlayer(const std::string &filePath,
                                                 double outputSampleRate)
    : filePath(filePath), outputSampleRate(outputSampleRate) {
  if (!loadAudioFile()) {
    return;
  }

  std::cout << "File: " << filePath << std::endl;
  std::cout << "  Sample rate: " << fileSampleRate << " Hz" << std::endl;
  std::cout << "  Channels: " << numChannels << std::endl;
  std::cout << "  Frames: " << totalFrames << std::endl;
  std::cout << "  Duration: " << std::fixed << std::setprecision(1)
            << (double)totalFrames / fileSampleRate << "s" << std::endl;
  std::cout << "  Memory: " << std::fixed << std::setprecision(1)
            << (audioData.size() * sizeof(float)) / (1024.0 * 1024.0) << " MB" << std::endl;

  bool needsResampling = (std::abs(fileSampleRate - outputSampleRate) > 0.1);
  if (needsResampling) {
    double ratio = fileSampleRate / outputSampleRate;
    std::cout << "  Resampling: " << std::fixed << std::setprecision(3)
              << ratio << "x" << std::endl;
  }
}

BufferedAudioFilePlayer::~BufferedAudioFilePlayer()
{
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

        // Load entire file into memory (interleaved format)
        std::cout << "Loading entire file into memory..." << std::flush;

        audioData.resize(totalFrames * numChannels);

        // Load in chunks to show progress and avoid timeout
        const uint64_t chunkSize = 48000 * 10; // 10 seconds at 48kHz
        uint64_t framesLoaded = 0;

        while (framesLoaded < totalFrames)
        {
            uint64_t framesToRead = std::min(chunkSize, totalFrames - framesLoaded);

            // Create buffer for reading
            choc::buffer::ChannelArrayBuffer<float> readBuffer(
                choc::buffer::Size::create(numChannels, static_cast<uint32_t>(framesToRead)));

            // Read chunk
            auto readView = readBuffer.getView();
            if (!fileReader->readFrames(framesLoaded, readView))
            {
                errorMessage = "Failed to read audio data";
                return false;
            }

            // Convert to interleaved format
            for (uint64_t frame = 0; frame < framesToRead; ++frame)
            {
                for (uint32_t channel = 0; channel < numChannels; ++channel)
                {
                    audioData[(framesLoaded + frame) * numChannels + channel] = readView.getSample(channel, frame);
                }
            }

            framesLoaded += framesToRead;

            // Progress indicator
            int percent = (int)((framesLoaded * 100) / totalFrames);
            std::cout << "\rLoading entire file into memory... " << percent << "%" << std::flush;
        }

        std::cout << std::endl;

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

void BufferedAudioFilePlayer::processBlock(choc::buffer::ChannelArrayView<float> output)
{
    output.clear();

    if (!fileLoaded || audioData.empty()) return;

    auto numFrames = output.getNumFrames();
    auto numOutputChannels = output.getNumChannels();

    uint64_t currentPos = playbackPosition.load();

    // Read samples directly from memory (interleaved) and write to output (channel array)
    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        // Handle looping
        if (currentPos >= totalFrames)
        {
            currentPos = 0;
        }

        // Read interleaved samples from memory
        uint64_t sampleIndex = currentPos * numChannels;

        for (uint32_t channel = 0; channel < numOutputChannels; ++channel)
        {
            uint32_t sourceChannel = std::min(channel, numChannels - 1);
            output.getSample(channel, frame) = audioData[sampleIndex + sourceChannel];
        }

        currentPos++;
    }

    // Update playback position
    playbackPosition.store(currentPos);
}
