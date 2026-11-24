#pragma once
#include "../Window.h"
#include <JuceHeader.h>
#include <functional>

class SettingsWindow : public AetherWindow
{
public:
    // Pass your app's AudioDeviceManager to enable the "Audio Device" page.
    explicit SettingsWindow (juce::AudioDeviceManager* deviceManager = nullptr);

    // The HRIR page calls this when the user presses "Use this profile".
    // Set it once from the code that owns the Timeline, e.g.
    //   SettingsWindow::onHrirChosen = [this] (const juce::String& f) { loadDefaultHrir (f); };
    // and reset it to nullptr when the Timeline is destroyed.
    static inline std::function<void (const juce::String& hrirFileName)> onHrirChosen;

    // The profile currently in use (file name inside assets/hrtf).
    static inline juce::String activeHrirFile { "d1.hrir" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};