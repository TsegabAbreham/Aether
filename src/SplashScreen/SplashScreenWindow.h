#pragma once

#include <JuceHeader.h>
#include "SplashScreen.h"

class SplashScreenWindow : public juce::DocumentWindow
{
public:
    SplashScreenWindow();
    ~SplashScreenWindow() override;

    void setProgress(double progress, const juce::String& status = {}) { splashScreen.setProgress(progress, status); }

    // Fires after the bar reaches 100%. Close/delete the splash and open your main window here.
    std::function<void()>& onFinished() { return splashScreen.onFinished; }

    void closeButtonPressed() override {}

private:
    AetherSplashScreen splashScreen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreenWindow)
};