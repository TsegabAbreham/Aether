
#pragma once

#include <JuceHeader.h>

class AetherWindow : public juce::DocumentWindow
{
public:
    AetherWindow(
        const juce::String& title,
        int width,
        int height
    );

    ~AetherWindow() override = default;

    void closeButtonPressed() override;

protected:
    virtual void onClose();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherWindow)
};
