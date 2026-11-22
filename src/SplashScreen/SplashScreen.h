
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

class AetherSplashScreen : public juce::Component,
                           private juce::Timer
{
public:

    AetherSplashScreen();

    ~AetherSplashScreen() override
    {
        stopTimer();
    }

    // Progress: 0.0 - 1.0
    // The progress bar smoothly animates toward this value.
    void setProgress(double newProgress,
                    const juce::String& statusText = {});
                    
    double getProgress() const
    {
        return targetProgress;
    }

    // Called once after progress reaches 100%.
    std::function<void()> onFinished;

    void paint(juce::Graphics& g) override;


private:

    // Timer
    void timerCallback() override;


    // Painting functions
    void paintBackground(
        juce::Graphics& g,
        juce::Rectangle<float> r
    );

    void paintRipples(
        juce::Graphics& g,
        juce::Point<float> centre
    );

    void paintWaveform(
        juce::Graphics& g,
        juce::Rectangle<float> r,
        float y
    );

    void paintProgress(
        juce::Graphics& g,
        juce::Rectangle<float> bar
    );


    // Background particles
    struct Particle
    {
        float x;
        float y;
        float z;
        float speed;
        float twinkle;
    };

    std::vector<Particle> particles;

    juce::Random rng;


    // Images
    juce::Image backgroundImage;
    juce::Image emblem;


    // Progress
    double targetProgress = 0.0;
    double shownProgress = 0.0;


    // Animation
    float phase = 0.0f;

    double lastTickMs = 0.0;
    double finishedSince = -1.0;

    bool finishedFired = false;


    // Status
    juce::String status { "Starting..." };


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        AetherSplashScreen
    )
};
