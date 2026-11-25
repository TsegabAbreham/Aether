#pragma once

#include <JuceHeader.h>

#include "components/knob.h"
#include "components/Menubar/MenuBar.h"

#include "components/Workspace/Workspace.h"

#include "Audio/core/AudioEngine.h"

#include <string>

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
    std::string savedStateSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};