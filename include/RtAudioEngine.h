#include "AudioEngine.h"
#include "rtaudio/RtAudio.h"

//==============================================================================
/// RtAudio implementation of the AudioEngineInterface.
/// Provides cross-platform audio output using the RtAudio library, with support
/// for CoreAudio (macOS), ALSA (Linux), WASAPI (Windows), and other backends.
class RtAudioEngine : public AudioEngineInterface
{
public:
    RtAudioEngine() : rtAudio (nullptr) {}

    ~RtAudioEngine() override
    {
        stop();
    }

    std::vector<AudioDeviceInfo> getAvailableOutputDevices() override
    {
        std::vector<AudioDeviceInfo> devices;

        try
        {
            RtAudio audio;
            auto deviceIds = audio.getDeviceIds();
            unsigned int defaultOutputId = audio.getDefaultOutputDevice();

            for (auto id : deviceIds)
            {
                auto info = audio.getDeviceInfo (id);
                if (info.outputChannels > 0)  // Only output devices
                {
                    AudioDeviceInfo device;
                    device.id = id;
                    device.name = info.name;
                    device.outputChannels = info.outputChannels;
                    device.isDefault = (id == defaultOutputId);
                    device.preferredSampleRate = info.preferredSampleRate;
                    device.supportedSampleRates = info.sampleRates;
                    devices.push_back (device);
                }
            }
        }
        catch (const std::exception& e)
        {
            lastError = std::string ("Error enumerating devices: ") + e.what();
        }

        return devices;
    }

    AudioDeviceInfo getDefaultOutputDevice() override
    {
        auto devices = getAvailableOutputDevices();
        for (const auto& device : devices)
        {
            if (device.isDefault)
                return device;
        }

        // Fallback to first device if no default found
        if (! devices.empty())
            return devices[0];

        // Return empty device if none found
        return AudioDeviceInfo{0, "", 0, false, 0, {}};
    }

    bool start (const AudioEngineOptions& options = {}) override
    {
        if (rtAudio && rtAudio->isStreamOpen())
        {
            lastError = "Stream already open";
            return false;
        }

        try
        {
            rtAudio = std::make_unique<RtAudio>();

            // Use default device if deviceId is 0
            unsigned int deviceId = options.outputDeviceId;
            if (deviceId == 0)
                deviceId = rtAudio->getDefaultOutputDevice();

            // Verify device exists and supports output
            auto deviceInfo = rtAudio->getDeviceInfo (deviceId);
            if (deviceInfo.outputChannels == 0)
            {
                lastError = "Device " + std::to_string (deviceId) + " does not support output";
                return false;
            }

            // Set up stream parameters
            RtAudio::StreamParameters outputParams;
            outputParams.deviceId = deviceId;
            outputParams.nChannels = options.outputChannels;
            outputParams.firstChannel = 0;

            // Set up stream options
            RtAudio::StreamOptions streamOptions;
            if (options.minimizeLatency)
                streamOptions.flags = RTAUDIO_MINIMIZE_LATENCY;
            streamOptions.numberOfBuffers = 2;
            streamOptions.streamName = "RtAudioEngine";

            // Store requested settings
            currentBlockSize = options.blockSize;
            currentSampleRate = options.sampleRate;

            // Open the stream
            rtAudio->openStream (&outputParams, nullptr, RTAUDIO_FLOAT32,
                               options.sampleRate, &currentBlockSize,
                               &RtAudioEngine::staticAudioCallback, this, &streamOptions);

            // Update with actual values set by RtAudio
            currentSampleRate = options.sampleRate;

            // Notify callback of sample rate
            if (audioCallback)
                audioCallback->sampleRateChanged (currentSampleRate);

            // Start the stream
            rtAudio->startStream();

            // Notify callback that audio started
            if (audioCallback)
                audioCallback->audioStarted();

            return true;
        }
        catch (const std::exception& e)
        {
            lastError = std::string ("RtAudio error: ") + e.what();
            rtAudio.reset();
            return false;
        }
    }

    void stop() override
    {
        if (rtAudio)
        {
            try
            {
                if (rtAudio->isStreamRunning())
                    rtAudio->stopStream();
                if (rtAudio->isStreamOpen())
                    rtAudio->closeStream();
            }
            catch (const std::exception& e)
            {
                lastError = std::string ("Error stopping RtAudio stream: ") + e.what();
            }
            rtAudio.reset();

            // Notify callback that audio stopped
            if (audioCallback)
                audioCallback->audioStopped();
        }
    }

    bool isRunning() const override
    {
        return rtAudio && rtAudio->isStreamRunning();
    }

    unsigned int getCurrentBlockSize() const override { return currentBlockSize; }
    double getCurrentSampleRate() const override { return currentSampleRate; }
    std::string getLastError() const override { return lastError; }

    void setCallback (AudioEngineCallback* callback) override
    {
        audioCallback = callback;
    }

    AudioEngineCallback* getCallback() const override
    {
        return audioCallback;
    }

private:
    std::unique_ptr<RtAudio> rtAudio;

    /// RtAudio callback function - called on audio thread
    static int staticAudioCallback (void* outputBuffer, void* inputBuffer,
                                  unsigned int nFrames, double streamTime,
                                  RtAudioStreamStatus status, void* userData)
    {
        auto* engine = static_cast<RtAudioEngine*> (userData);

        // Handle underruns/overruns
        if (status)
            std::cout << "RtAudio stream status: " << status << std::endl;

        if (engine->audioCallback)
        {
            // Create interleaved view for the output buffer
            auto outputView = choc::buffer::createInterleavedView<float> (
                static_cast<float*> (outputBuffer),
                2,  // TODO: Make this configurable based on actual channel count
                nFrames);

            // Let the callback render the audio
            engine->audioCallback->renderAudio (outputView);
        }

        return 0;  // Continue stream
    }
};
