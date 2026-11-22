#pragma once

#include <JuceHeader.h>

#include "components/knob.h"
#include "components/MenuBar.h"

#include "components/Workspace/Workspace.h"

#include "Audio/core/AudioEngine.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MenuBar menuBar;
    Workspace workspace;
    AudioEngine audioEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};