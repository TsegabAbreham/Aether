#include "Knob.h"

CustomKnob::CustomKnob(juce::String name)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    knob.setRange(0.0, 100.0, 0.1);
    knob.setValue(50.0);

    knob.setLookAndFeel(&metallicLookAndFeel);
    addAndMakeVisible(knob);

    knobLabel.setText(name, juce::dontSendNotification);
    knobLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(knobLabel);
}

CustomKnob::~CustomKnob()
{
    knob.setLookAndFeel(nullptr);
}

void CustomKnob::resized()
{
    auto area = getLocalBounds();

    knobLabel.setBounds(area.removeFromBottom(25));
    knob.setBounds(area);
}


void MetallicLookAndFeel::drawRotarySlider(
    juce::Graphics& g,
    int x, int y,
    int width, int height,
    float sliderPos,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider& slider)

{
    juce::ignoreUnused(slider);

    auto bounds = juce::Rectangle<float>(
        (float)x, (float)y,
        (float)width, (float)height
    ).reduced(8.0f);

    float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());

    float radius = diameter * 0.5f;
    float centreX = bounds.getCentreX();
    float centreY = bounds.getCentreY();

    float angle = rotaryStartAngle +
        sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Metallic outer ring
    auto outerBounds = juce::Rectangle<float>(
        centreX - radius,
        centreY - radius,
        diameter,
        diameter
    );

    g.setGradientFill(
        juce::ColourGradient(
            juce::Colour(230, 235, 240),
            centreX, centreY - radius,
            juce::Colour(65, 72, 82),
            centreX, centreY + radius,
            false
        )
    );

    g.fillEllipse(outerBounds);

    // Dark metallic border
    g.setColour(juce::Colour(25, 29, 36));
    g.drawEllipse(outerBounds, 1.5f);

    // Inner knob face
    auto faceBounds = outerBounds.reduced(diameter * 0.12f);

    g.setGradientFill(
        juce::ColourGradient(
            juce::Colour(105, 115, 128),
            centreX, centreY - radius,
            juce::Colour(30, 35, 44),
            centreX, centreY + radius,
            false
        )
    );

    g.fillEllipse(faceBounds);

    // Subtle inner rim
    g.setColour(juce::Colour(175, 185, 195).withAlpha(0.35f));
    g.drawEllipse(faceBounds, 1.2f);

    // Tick marks
    const int numberOfTicks = 21;

    float tickOuterRadius = radius * 0.94f;
    float tickInnerRadius = radius * 0.82f;

    for (int i = 0; i < numberOfTicks; ++i)
    {
        float proportion = (float)i / (numberOfTicks - 1);

        float tickAngle = rotaryStartAngle +
            proportion * (rotaryEndAngle - rotaryStartAngle);

        float outerX = centreX + tickOuterRadius * std::sin(tickAngle);
        float outerY = centreY - tickOuterRadius * std::cos(tickAngle);

        float innerX = centreX + tickInnerRadius * std::sin(tickAngle);
        float innerY = centreY - tickInnerRadius * std::cos(tickAngle);

        g.setColour(
            proportion <= sliderPos
                ? juce::Colour(80, 210, 180)
                : juce::Colour(170, 180, 190)
        );

        g.drawLine(
            innerX, innerY,
            outerX, outerY,
            i % 5 == 0 ? 2.0f : 1.0f
        );
    }

    // Active value arc
    juce::Path valueArc;

    valueArc.addCentredArc(
        centreX,
        centreY,
        radius * 0.73f,
        radius * 0.73f,
        0.0f,
        rotaryStartAngle,
        angle,
        true
    );

    g.setColour(juce::Colour(70, 220, 185));
    g.strokePath(
        valueArc,
        juce::PathStrokeType(2.5f)
    );

    // Indicator
    float pointerLength = radius * 0.65f;
    float pointerThickness = diameter * 0.035f;

    juce::Path pointer;

    pointer.addRoundedRectangle(
        -pointerThickness * 0.5f,
        -radius * 0.68f,
        pointerThickness,
        pointerLength,
        pointerThickness * 0.5f
    );

    g.setColour(juce::Colour(230, 240, 245));

    g.fillPath(
        pointer,
        juce::AffineTransform::rotation(angle)
            .translated(centreX, centreY)
    );

    // Centre cap
    float capRadius = radius * 0.12f;

    g.setGradientFill(
        juce::ColourGradient(
            juce::Colour(190, 200, 210),
            centreX, centreY - capRadius,
            juce::Colour(55, 65, 78),
            centreX, centreY + capRadius,
            false
        )
    );

    g.fillEllipse(
        centreX - capRadius,
        centreY - capRadius,
        capRadius * 2.0f,
        capRadius * 2.0f
    );

    g.setColour(juce::Colours::black.withAlpha(0.5f));

    g.drawEllipse(
        centreX - capRadius,
        centreY - capRadius,
        capRadius * 2.0f,
        capRadius * 2.0f,
        1.0f
    );
}

