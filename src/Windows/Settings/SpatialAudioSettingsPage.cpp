#include "SettingsPageFactory.h"
#include "SettingsPageComponents.h"

namespace SettingsPages
{
    std::unique_ptr<juce::Component> makeSpatialAudioPage()
    {
        using namespace SettingsUI;
        return makePanel ("Spatial Audio", {
            new ToggleRow ("Binaural rendering", Key::BinauralEnabled),
            new SliderRow ("Master gain", -24.0, 12.0, 0.5, Key::MasterGainDb, " dB"),
            new ChoiceRow ("Distance attenuation",
                           juce::StringArray { "None", "Inverse", "Linear" },
                           Key::DistanceModel),
            new ToggleRow ("Doppler effect", Key::DopplerEnabled),
            new ToggleRow ("Output limiter", Key::LimiterEnabled) });
    }
}
