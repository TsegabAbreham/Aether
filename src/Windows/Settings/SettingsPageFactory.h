#pragma once

#include <JuceHeader.h>
#include <memory>

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeHrirPage();
    std::unique_ptr<juce::Component> makeSpatialAudioPage();
    std::unique_ptr<juce::Component> makeAudioDevicePage (juce::AudioDeviceManager*);
    std::unique_ptr<juce::Component> makeViewportPage();
    std::unique_ptr<juce::Component> makeTimelinePage();
    std::unique_ptr<juce::Component> makeGeneralPage();
}
