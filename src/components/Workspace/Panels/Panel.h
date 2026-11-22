#pragma once
#include <JuceHeader.h>

class Panel : public juce::Component
{
public:
    explicit Panel(juce::String title);
    ~Panel() override = default;

    void resized() override;

    void setContent(juce::Component* component);

protected:
    juce::Component* content = nullptr;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Panel)
};