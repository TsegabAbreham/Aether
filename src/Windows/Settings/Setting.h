#pragma once
#include "../Window.h"
#include <JuceHeader.h>

class SettingsWindow : public AetherWindow
{
public:
    // Pass your app's AudioDeviceManager to enable the "Audio Device" page.
    explicit SettingsWindow (juce::AudioDeviceManager* deviceManager = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};