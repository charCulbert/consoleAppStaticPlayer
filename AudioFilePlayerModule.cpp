#include "AudioFilePlayerModule.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_AudioFileFormat.h"
#include <stdexcept>
#include <iostream>

AudioFilePlayerModule::AudioFilePlayerModule(const std::string& filePath)
    : filePath(filePath)
{
    // Open the file stream.
    auto fileStream = std::make_shared<std::ifstream>(filePath, std::ios::binary);
    if (!fileStream || !fileStream->is_open())
    {
        std::cerr << "Error: could not open file: " << filePath << "\n";
        throw std::runtime_error("Cannot open file");
    }

    try {
        // Create a file format list and add the WAV format.
        choc::audio::AudioFileFormatList formatList;
        formatList.addFormat<choc::audio::WAVAudioFileFormat<false>>();

        // Attempt to load the file using the registered format.
        audioData = formatList.loadFileContent(fileStream, 48000);
    } catch (const std::exception& e) {
        std::cerr << "Error loading audio file: " << e.what() << std::endl;
        throw;
    }
}

void AudioFilePlayerModule::prepareToPlay(int samplesPerBlock, double sampleRate)
{
    currentSampleRate = sampleRate;
    readPosition = 0;
    // Assuming the WAV file already matches the engine's sample rate.
}

void AudioFilePlayerModule::releaseResources()
{
    // No additional resources to release.
}

void AudioFilePlayerModule::render(choc::buffer::InterleavedView<float> outputBuffer)
{
    const auto numOutFrames   = outputBuffer.getNumFrames();
    const auto numOutputChans = outputBuffer.getNumChannels();
    const uint32_t availableFrames = audioData.frames.getNumFrames();
    const uint32_t fileChans = audioData.frames.getNumChannels();

    // Safety: if no frames loaded, do nothing.
    if (availableFrames == 0 || fileChans == 0)
        return;

    // Get pointer to the output data.
    float* outData = outputBuffer.data.data;

    for (uint32_t frame = 0; frame < numOutFrames; ++frame)
    {
        for (uint32_t ch = 0; ch < numOutputChans; ++ch)
        {
            float sample = 0.0f;
            if (readPosition < availableFrames)
            {
                // If the file doesn't have as many channels as output, use channel 0.
                auto effectiveChannel = (ch < fileChans) ? ch : 0;
                auto channelView = audioData.frames.getChannel(effectiveChannel);
                sample = channelView.getSample(0, readPosition);
            }
            outData[frame * numOutputChans + ch] += sample;
        }
        ++readPosition;
        if (readPosition >= availableFrames)
            readPosition = 0;
    }
}

