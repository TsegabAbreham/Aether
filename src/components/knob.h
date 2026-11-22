#pragma once
#include <JuceHeader.h>

class MetallicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(
        juce::Graphics& g,
        int x, int y,
        int width, int height,
        float sliderPos,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& slider
    ) override;
};

class CustomKnob : public juce::Component
{
public:
    CustomKnob(juce::String name);
    ~CustomKnob() override;

    void resized() override;

    double getValue() const
    {
        return knob.getValue();
    }

    void setRange(double min, double max, double interval)
    {
        knob.setRange(min, max, interval);
    }

    void setValue(double value)
    {
        knob.setValue(value);
    }

    void setOnValueChange(std::function<void(double)> callback)
    {
        knob.onValueChange = [this, callback]()
        {
            if (callback)
                callback(knob.getValue());
        };
    }

private:
    juce::Slider knob;
    MetallicLookAndFeel metallicLookAndFeel;
    juce::Label knobLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomKnob)
};