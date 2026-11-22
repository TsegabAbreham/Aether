
#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    // Register WAV, AIFF and other standard formats.
    formatManager.registerBasicFormats();

    // Initialize audio output.
    // 0 input channels, 2 output channels.
    auto error = deviceManager.initialise(
        0,
        2,
        nullptr,
        true
    );

    if (error.isNotEmpty())
        juce::Logger::writeToLog(error);

    // Connect our transport source to the output.
    sourcePlayer.setSource(&transportSource);

    // Start receiving audio callbacks.
    deviceManager.addAudioCallback(&sourcePlayer);
}

AudioEngine::~AudioEngine()
{
    // Disconnect audio before destroying resources.
    deviceManager.removeAudioCallback(&sourcePlayer);

    sourcePlayer.setSource(nullptr);

    transportSource.stop();
    transportSource.setSource(nullptr);

    readerSource.reset();
}


// ========================================
// LOAD AUDIO FILE
// ========================================

bool AudioEngine::loadFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    // Attempt to open the audio file.
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file)
    );

    if (reader == nullptr)
        return false;

    // Stop the previous audio.
    transportSource.stop();

    // Disconnect the previous reader.
    transportSource.setSource(nullptr);

    // Destroy the previous audio source.
    readerSource.reset();

    // Remember the sample rate before transferring ownership.
    const double sampleRate = reader->sampleRate;

    // Create a new reader source.
    readerSource =
        std::make_unique<juce::AudioFormatReaderSource>(
            reader.release(),
            true
        );

    // Connect the reader to the transport.
    transportSource.setSource(
        readerSource.get(),
        0,
        nullptr,
        sampleRate
    );

    return true;
}


// ========================================
// UNLOAD AUDIO
// ========================================

void AudioEngine::unloadFile()
{
    transportSource.stop();
    transportSource.setSource(nullptr);

    readerSource.reset();
}


// ========================================
// PLAYBACK
// ========================================

void AudioEngine::play()
{
    transportSource.start();
}

void AudioEngine::pause()
{
    transportSource.stop();
}

void AudioEngine::stop()
{
    transportSource.stop();
    transportSource.setPosition(0.0);
}

bool AudioEngine::isPlaying() const
{
    return transportSource.isPlaying();
}


// ========================================
// PLAYBACK POSITION
// ========================================

void AudioEngine::setPosition(double seconds)
{
    transportSource.setPosition(seconds);
}

double AudioEngine::getPosition() const
{
    return transportSource.getCurrentPosition();
}

double AudioEngine::getLength() const
{
    return transportSource.getLengthInSeconds();
}


// ========================================
// VOLUME
// ========================================

void AudioEngine::setGain(float gain)
{
    transportSource.setGain(
        juce::jlimit(0.0f, 1.0f, gain)
    );
}
