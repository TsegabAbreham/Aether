#include "SettingsPageFactory.h"

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeAudioDevicePage (juce::AudioDeviceManager* deviceManager)
    {
        if (deviceManager != nullptr)
            return std::make_unique<juce::AudioDeviceSelectorComponent> (
                *deviceManager, 0, 0, 2, 2, false, false, true, false);

        auto message = std::make_unique<juce::Label> (
            juce::String(), "No audio device manager was provided.");
        message->setJustificationType (juce::Justification::centred);
        return message;
    }
}
