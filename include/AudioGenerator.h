#include "choc/audio/choc_AudioMIDIBlockDispatcher.h"
#include "choc/audio/choc_Oscillators.h"
#include "choc/audio/io/choc_AudioMIDIPlayer.h"


struct AudioGenerator    : public choc::audio::io::AudioMIDICallback
{
    AudioGenerator() : squareWave()
    {
        // Will be properly set by sampleRateChanged callback
        squareWave.setFrequency (440.0f, 44100.0f);
    }

    void sampleRateChanged (double newRate) override
    {
        squareWave.setFrequency (440.0f, static_cast<float> (newRate));
    }

    void startBlock() override
    {
    }

    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block& block,
                          bool replaceOutput) override
    {
        auto& output = block.audioOutput;

        if (! isPlaying)
        {
            if (replaceOutput)
                output.clear();

            return;
        }

        auto numFrames = output.getNumFrames();
        auto numChannels = output.getNumChannels();

        for (uint32_t frame = 0; frame < numFrames; ++frame)
        {
            if (samplesPlayed >= maxSamples)
            {
                isPlaying = false;

                for (uint32_t channel = 0; channel < numChannels; ++channel)
                    output.getSample (channel, frame) = 0.0f;

                continue;
            }

            auto sample = squareWave.getSample() * 0.1f;

            for (uint32_t channel = 0; channel < numChannels; ++channel)
            {
                if (replaceOutput)
                    output.getSample (channel, frame) = sample;
                else
                    output.getSample (channel, frame) += sample;
            }

            ++samplesPlayed;
        }
    }

    void endBlock() override
    {
    }

    bool isStillPlaying() const         { return isPlaying; }

private:
    choc::oscillator::Square<float> squareWave;
    bool isPlaying = true;
    uint32_t samplesPlayed = 0;
    uint32_t maxSamples = 44100 * 2;
};
