#include "../include/AudioFilePlayerModule.h"
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

        // First load the file at its native sample rate to detect it
        audioData = formatList.loadFileContent(fileStream);
        
        // Debug: check what audioData.sampleRate actually contains
        std::cout << "DEBUG: audioData.sampleRate = " << audioData.sampleRate << " Hz" << std::endl;
        
        // Store the native sample rate immediately to prevent corruption
        nativeSampleRate = audioData.sampleRate;
        
        std::cout << "DEBUG: nativeSampleRate = " << nativeSampleRate << " Hz" << std::endl;
        
        std::cout << "Loaded audio file: " << filePath << std::endl;
        std::cout << "  Native sample rate: " << nativeSampleRate << " Hz" << std::endl;
        std::cout << "  Channels: " << audioData.frames.getNumChannels() << std::endl;
        std::cout << "  Frames: " << audioData.frames.getNumFrames() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading audio file: " << e.what() << std::endl;
        throw;
    }
}

void AudioFilePlayerModule::prepareToPlay(int samplesPerBlock, double sampleRate)
{
    currentSampleRate = sampleRate;
    readPosition = 0;
    
    // Check if we need to resample the audio data
    resampleIfNeeded(sampleRate);
}

void AudioFilePlayerModule::resampleIfNeeded(double targetSampleRate)
{
    // If sample rates match within tolerance, no resampling needed
    if (std::abs(audioData.sampleRate - targetSampleRate) < 0.1)
    {
        std::cout << "  No resampling needed - rates match!" << std::endl;
        return;
    }
    
    std::cout << "  Resampling from " << audioData.sampleRate << " Hz to " << targetSampleRate << " Hz..." << std::endl;
    
    try 
    {
        // Reload the file and let choc resample it to the target rate
        auto fileStream = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!fileStream || !fileStream->is_open())
        {
            std::cerr << "Error: could not reopen file for resampling: " << filePath << std::endl;
            return;
        }

        choc::audio::AudioFileFormatList formatList;
        formatList.addFormat<choc::audio::WAVAudioFileFormat<false>>();
        
        // Load with target sample rate - choc will handle resampling
        audioData = formatList.loadFileContent(fileStream, targetSampleRate);
        
        std::cout << "  Resampling complete! New rate: " << audioData.sampleRate << " Hz" << std::endl;
        
        // Reset read position after resampling
        readPosition = 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error during resampling: " << e.what() << std::endl;
    }
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

