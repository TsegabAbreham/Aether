#include "Panel.h"

Panel::Panel(juce::String title)
{
    juce::ignoreUnused(title);
}

void Panel::resized()
{
    if (content != nullptr)
        content->setBounds(getLocalBounds().reduced(10));
}

void Panel::setContent(juce::Component* component)
{
    if (content != nullptr)
        removeChildComponent(content);

    content = component;

    if (content != nullptr)
        addAndMakeVisible(content);

    resized();
}