#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(menuBar);
    addAndMakeVisible(workspace);

    menuBar.onFolderSelected = [this](juce::File folder)
    {
        workspace.setProjectFolder(folder);
    };

    setSize(800, 600);
}

MainComponent::~MainComponent()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(25, 25, 28));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    menuBar.setBounds(area.removeFromTop(30));

    workspace.setBounds(area);
}