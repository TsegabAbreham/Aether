
#pragma once

#include <JuceHeader.h>

class AudioEngine
{
public:

    AudioEngine();
    ~AudioEngine();

    // Audio file management
    bool loadFile(const juce::File& file);
    void unloadFile();

    // Playback
    void play();
    void pause();
    void stop();

    // Playback position
    void setPosition(double seconds);
    double getPosition() const;
    double getLength() const;

    // Volume
    void setGain(float gain);

    bool isPlaying() const;

private:

    // Handles the computer's audio device
    juce::AudioDeviceManager deviceManager;

    // Sends audio to the output device
    juce::AudioSourcePlayer sourcePlayer;

    // Controls playback
    juce::AudioTransportSource transportSource;

    // Reads supported audio formats
    juce::AudioFormatManager formatManager;

    // Holds the currently loaded audio file
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
