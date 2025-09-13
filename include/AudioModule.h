//
// Created by charTP on 21/02/2025.
//
#pragma once

#include "choc/audio/choc_SampleBuffers.h"

// Abstract base class for audio modules.
class AudioModule {
public:
    virtual ~AudioModule() = default;

    // Called once at the beginning to allow modules to allocate buffers, etc.
    virtual void prepareToPlay(int samplesPerBlock, double sampleRate) {}

    // Called when playback stops to clean up any resources.
    virtual void releaseResources() {}

    virtual double getNativeSampleRate() const { return 0.0; }

    // Render audio into the provided output view.
    // A module is responsible for adding (or overwriting) its audio data into the buffer.
    virtual void render(choc::buffer::InterleavedView<float> outputBuffer) = 0;
};
